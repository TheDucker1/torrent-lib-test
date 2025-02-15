#include"media_torrent_manager.hpp"
#include"media_test.hpp"
#include"media_mkv.hpp"
#include<filesystem>
#include<fstream>

media_torrent_manager::media_torrent_manager(lt::session_params const& ses_params) :
    m_session(ses_params) {
}

void media_torrent_manager::handle_loop() {
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<lt::alert*> alerts;
    m_session.pop_alerts(&alerts);

    for (lt::alert const* al : alerts) {
        std::cerr << "[AL MSG]: " << al->message() << std::endl;
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
            piece_fin_al->handle.read_piece(piece_fin_al->piece_index);
        }
        if (auto piece_read_al = lt::alert_cast<lt::read_piece_alert>(al)) {
            if (!piece_read_al->error) {
                std::cerr << "RECEIVE PIECE " << piece_read_al->piece << "\n";
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

    for (auto& media: m_media_list) {
        media->process();
    }
    for (auto it = m_media_list.begin(), nx = std::next(it); it != m_media_list.end(); it = nx) {
        nx = std::next(it);
        if ((*it)->is_finish()) {
            m_file_counter[(*it)->get_torrent_handle().info_hash()] -= 1;
            m_media_list.erase(it);
        }
    }

    for (auto it = m_active_list.begin(), nx = std::next(it); it != m_active_list.end(); it = nx) {
        nx = std::next(it);
        if (m_file_counter[it->info_hash()] == 0) {
            m_file_counter.erase(it->info_hash());
            m_upload_list.emplace_back(*it);
            it->set_flags(lt::torrent_flags::upload_mode);

            m_active_list.erase(it);
        }
    }

    auto cur_clock = std::chrono::steady_clock::now();
    for (auto it = m_upload_list.begin(), nx = std::next(it); it != m_upload_list.end(); it = nx) {
        nx = std::next(it);
        if (cur_clock - it->add_tm > std::chrono::minutes{30}) {
            std::cerr << "remove torrent\n";
            m_session.remove_torrent(it->handle);
            m_upload_list.erase(it);
        }
    }
};

void media_torrent_manager::handle_torrent_add(lt::torrent_handle const& h) {
    //the torrent isn't init yet
    if (h.torrent_file() == nullptr) {
        return;
    }
    h.unset_flags(lt::torrent_flags::auto_managed);
    check_resume_data(h);
    h.unset_flags(lt::torrent_flags::upload_mode);

    auto it = m_pending_update.find(h.info_hash());
    if (it != m_pending_update.end()) {
        m_upload_list.emplace_back(h);
        m_pending_update.erase(it);
        return;
    }

    m_active_list.emplace_back(h);
    std::shared_ptr<lt::torrent_info const> ti = h.torrent_file();
    lt::file_storage const& fs = ti->files();
    for (lt::file_index_t i: fs.file_range()) {
        if (fs.pad_file_at(i)) continue;
        std::string sv(fs.file_name(i));
        if (!is_support_media(sv)) continue;
        handle_file_add(h, i, sv);
        m_file_counter[h.info_hash()] += 1;
    }
}
void media_torrent_manager::handle_file_add(
    lt::torrent_handle const& h,
    lt::file_index_t const index,
    std::string const& fn
) {
    std::cerr << "ADDED FILE [ " << fn << " ] to manager" << std::endl;

    // based on file name, choose appropriate type
    m_media_list.emplace_back(std::make_unique<media_mkv>(h, index));
}

void media_torrent_manager::add_torrent_download(
    std::string const& maybe_magnet_uri_or_torrent_file_path,
    int const upload_mode 
    ) {
    std::lock_guard<std::mutex> guard(m_mutex);

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

    if (upload_mode) {
        if (is_magnet) m_pending_update.insert(atp.info_hashes.get(lt::protocol_version::V1));
        else m_pending_update.insert(atp.ti->info_hash());
    }


    atp.save_path = m_save_path; // default save location
    atp.flags = lt::torrent_flags::default_dont_download |
        lt::torrent_flags::upload_mode; 
    atp.piece_priorities = 
        std::vector<lt::download_priority_t>(1, lt::dont_download);
    m_session.async_add_torrent(atp);
    return;
}

// or save and load a fast resume file, which is better
void media_torrent_manager::check_resume_data(lt::torrent_handle const &th) {
    std::string const th_save_name("." + 
            lt::aux::to_hex(th.info_hash()) + ".pieceset"); 

    std::filesystem::path fpth = std::filesystem::path(m_save_path) / 
            std::filesystem::path(th_save_name);
    std::ios::openmode mode_ = std::ios::binary | std::ios::in;

    std::fstream save_f(fpth, mode_);
    std::cerr << "Path: " << fpth << "\n";
    if (save_f.fail()) {
        std::cerr << " FOUND NO RESUME FILE \n";
        return;
    }

    std::vector<char> header(1024, '\0');
	char* ptr = header.data();
    char const* end_ptr = header.data() + header.size();
    save_f.read(header.data(), header.size());
    if (save_f.gcount() != 1024) return;
    std::int32_t const num_piece = lt::aux::read_int32(ptr);
    std::int32_t const piece_size = lt::aux::read_int32(ptr);
    std::vector<lt::piece_index_t> used_piece;
	for (int i = 0; i < num_piece; ++i) {
        if (ptr == end_ptr) {
            ptr = header.data();
            int byte_to_read = std::min(num_piece - i, 1024);
            save_f.read(header.data(), byte_to_read);
            if (save_f.gcount() != byte_to_read) {
                return;
            }
        }
		std::int8_t const used(lt::aux::read_int8(ptr));
		if (used) {
            used_piece.emplace_back(lt::piece_index_t(i));
        }
	}
    lt::file_storage const& fs = th.torrent_file()->files();
    std::vector<char> piece_data(fs.piece_length());
    for (lt::piece_index_t i : used_piece) {
        int this_piece_size = fs.piece_size(i);
        save_f.read(piece_data.data(), piece_data.size());
        if (save_f.gcount() != piece_data.size()) return;
        std::cerr << "[ADD PIECE " << i << "]\n";
        th.add_piece(i, piece_data.data());
        th.piece_priority(i, lt::default_priority);
    }
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

// only use filename, so make sure we have valid data
bool media_torrent_manager::is_support_media(
        std::string const& fn) const {

    if (fn.ends_with(".mkv")) return true;

    return false;
}
