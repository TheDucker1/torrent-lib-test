#include"media_torrent_manager.hpp"
#include"media_mkv.hpp"
#include<filesystem>
#include<fstream>

media_torrent_manager::media_torrent_manager(lt::session_params const& ses_params) :
    m_session(ses_params) {
}

void media_torrent_manager::handle_loop() {
    std::vector<lt::alert*> alerts;
    m_session.pop_alerts(&alerts);

    {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (lt::alert const* al : alerts) {
        // std::cerr << "[AL MSG]: " << al->message() << std::endl;
        if (auto add_al = lt::alert_cast<lt::add_torrent_alert>(al)) {
            if (add_al->error) {
                // error in adding, skip torrent
                continue;
            }
            if (add_al->params.ti != nullptr && add_al->params.ti->is_valid()) {
                handle_torrent_add(add_al->handle);
            }

        }
        if (auto meta_rec_al = lt::alert_cast<lt::metadata_received_alert>(al)) {
            handle_torrent_add(meta_rec_al->handle);
        }
        if (auto piece_fin_al = lt::alert_cast<lt::piece_finished_alert>(al)) {
            std::cerr << "FINISHE PIECE " << piece_fin_al->piece_index << "\n";
            if (piece_fin_al->handle == m_current_handle.handle)
                m_current_handle.update();
            piece_fin_al->handle.read_piece(piece_fin_al->piece_index);
        }
        if (auto piece_read_al = lt::alert_cast<lt::read_piece_alert>(al)) {
            if (!piece_read_al->error) {
                std::cerr << "RECEIVE PIECE " << piece_read_al->piece << "\n";
                if (piece_read_al->handle == m_current_handle.handle)
                    m_current_handle.update();

                handle_piece_receive(
                    piece_read_al->handle,
                    piece_read_al->piece,
                    piece_read_al->buffer,
                    piece_read_al->size
                );
            }
            else {
                std::cerr << "[SERIOUS ERROR]: Read goes very wrong" << std::endl;
            }
        }
        if (auto hash_fail_al = lt::alert_cast<lt::hash_failed_alert>(al)) {
            std::cerr << "HASH FAIL AT PIECE " << hash_fail_al->piece_index << "\n";
        }
        if (auto file_err_al = lt::alert_cast<lt::file_error_alert>(al)) {
            std::cerr << "IO ERROR AT FILE " << std::string(file_err_al->filename()) << " YY\n";
        }
    }
    }

    if (!m_current_handle.handle.is_valid() && m_pending_list.size() && m_pending_list.front().is_valid()) {
        set_handler_manager(m_pending_list.front()); // make sure the old file already got flush
        m_pending_list.pop_front();
    }
    for (auto& media: m_media_list) {
        media->process();
    }
    for (auto it = m_media_list.begin(), nx = std::next(it); it != m_media_list.end(); it = nx) {
        nx = std::next(it);
        if (it->get()->is_finish()) {
            m_current_handle.update();
            m_media_list.erase(it);
        }
    }
    if (m_media_list.empty()) {
        m_current_handle.set_upload();

        if (m_current_handle.handle.is_valid()) {
            lt::torrent_status cur_stat = m_current_handle.handle.status(lt::torrent_handle::status_flags_t(0));
            // std::cerr << cur_stat.all_time_upload << " / " << cur_stat.all_time_download << "\n";
            if (cur_stat.all_time_upload > cur_stat.all_time_download * 3 / 2) { // sufficient uploading
                    // thumbnail ~100MB -> should not overflow
                std::cerr << "remove torrent\n";
                m_session.remove_torrent(m_current_handle.handle);
            }
        }
    }


    auto cur_clock = std::chrono::steady_clock::now();
    if (cur_clock - m_current_handle.last_upd >= std::chrono::minutes{15}) { // time out
        for (auto it = m_media_list.begin(); it != m_media_list.end(); ++it) {
            it->get()->force_fail();
        }
        if (m_current_handle.handle.is_valid()) {
            std::cerr << "remove torrent\n";
            m_session.remove_torrent(m_current_handle.handle);
        }
    }
};

void media_torrent_manager::handle_torrent_add(lt::torrent_handle const& h) {
    //the torrent isn't init yet
    if (h.torrent_file() == nullptr) {
        return;
    }
    std::cerr << "ADDED NEW TORRENT\n";
    h.unset_flags(lt::torrent_flags::auto_managed);
    h.set_flags(lt::torrent_flags::upload_mode);
    m_pending_list.emplace_back(h);
}
void media_torrent_manager::set_handler_manager(lt::torrent_handle const& h) {
    if (m_current_handle.handle.is_valid()) return; // don't remove a valid handle
    m_current_handle = handle_manager(h);
    std::shared_ptr<lt::torrent_info const> ti = h.torrent_file();
    lt::file_storage const& fs = ti->files();
    for (lt::file_index_t i: fs.file_range()) {
        if (fs.pad_file_at(i)) continue;
        std::string sv(fs.file_name(i));
        if (!is_support_media(sv)) continue;
        handle_file_add(h, i, sv);
    }
    m_current_handle.set_download();

}
void media_torrent_manager::handle_file_add(
    lt::torrent_handle const& h,
    lt::file_index_t const index,
    std::string const& fn
) {
    std::cerr << "ADDED FILE [ " << fn << " ] to manager" << std::endl;

    m_media_list.emplace_back(std::make_unique<media_mkv>(h, index));
}

void media_torrent_manager::add_torrent_download(
    std::string const& maybe_magnet_uri_or_torrent_file_path
    ) {

    lt::error_code error;
    lt::add_torrent_params atp = lt::parse_magnet_uri(
            maybe_magnet_uri_or_torrent_file_path,
            error);
    int is_magnet = true;
    if (error) {
        // file is not a torrent
        is_magnet = false;
        if (error == lt::errors::unsupported_url_protocol) {
            std::shared_ptr<lt::torrent_info> ti = 
                std::make_shared<lt::torrent_info>(maybe_magnet_uri_or_torrent_file_path,
                        error);
            if (error) { // cannot load from disk, maybe buffer data? use another function
                return;
            }
            atp.ti = ti;
        }
        // missing or invalid info hash
        else {
            // not supported, for now
            return;
        }
    }

    atp.flags = lt::torrent_flags::default_dont_download |
        lt::torrent_flags::upload_mode; 
    atp.flags &= ~lt::torrent_flags::auto_managed; // make sure to clear the flag
    atp.piece_priorities = 
        std::vector<lt::download_priority_t>(1, lt::dont_download);
    std::lock_guard<std::mutex> guard(m_mutex);
    m_session.async_add_torrent(atp);
    return;
}

void media_torrent_manager::handle_piece_receive(
    lt::torrent_handle const& handle,
    lt::piece_index_t const idx,
    boost::shared_array<char> const piece_buffer,
    int const piece_size
) {
    for (auto& media : m_media_list) {
        if (media->get_torrent_handle() != handle) continue;
        media->receive_piece(idx, piece_buffer, piece_size);
    }
}

bool media_torrent_manager::is_support_media(
        std::string const& fn) const {

    if (fn.ends_with(".mkv")) return true;
    return false;
}
