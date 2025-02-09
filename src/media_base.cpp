#include"media_base.hpp"

media_base::media_base(
    lt::torrent_handle const& handle,
    lt::file_index_t const file_index
) : m_set_handle(true),
    m_last_receive(std::chrono::steady_clock::now()),
    m_mode(media_action_mode::INIT),
    m_handle(handle),
    m_file_index(file_index)
{}

void media_base::update_last_receive_to_now() {
    if (!m_set_handle) return;
    if (is_finish()) return;
    m_last_receive = std::chrono::steady_clock::now();
}
void media_base::check_last_receive_with_now() {
    if (!m_set_handle) return;
    if (is_finish()) return;
    auto current_clock = std::chrono::steady_clock::now();
    // it has been 60 seconds since we receive any new piece (~1MB?)
    // request them again [TODO]
    if ((current_clock - m_last_receive) > std::chrono::seconds{5}) {
        request_awaiting_pieces();
    }
}

void media_base::receive_piece(
        lt::piece_index_t const piece_index,
        boost::shared_array<char> const buf_ptr,
        int const buf_size) {
    if (!m_set_handle) return;
    auto it = m_awaiting_pieces.find(piece_index);
    if (it == m_awaiting_pieces.end()) return;

    m_awaiting_pieces.erase(it);
    m_piece_data[piece_index] = std::make_pair(buf_ptr, buf_size);
    update_last_receive_to_now();
}

void media_base::set_receive_pieces(
    std::vector<lt::piece_index_t> const& pieces_list
) {
    if (!m_set_handle) return;
    if (is_finish()) return;
    // either this or clear all awaiting pieces, test later
    if (m_awaiting_pieces.size()) return;

    for (lt::piece_index_t i: pieces_list) {
        // check for piece existance
        auto it = m_piece_data.find(i);
        if (it != m_piece_data.end()) continue;

        m_awaiting_pieces.insert(i);
        get_torrent_handle().piece_priority(i, lt::default_priority);
        //std::cerr << "PRIORITIZE PIECE [" << i << "] WITH PRIORITY " << lt::default_priority << std::endl;
    }
    update_last_receive_to_now();
}

std::vector<lt::piece_index_t> 
    media_base::map_byte_range(std::int64_t offset, std::int64_t size) const {
    if (!m_set_handle) return {};
    if (size <= 0) return {};

    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();

    std::int64_t file_offset_in_torrent = fs.file_offset(get_file_index());
    std::int64_t file_size_in_torrent = fs.file_size(get_file_index());

    TORRENT_ASSERT(file_size_in_torrent <= offset + size);
    std::int64_t piece_start_offset = file_offset_in_torrent + offset;
    std::int64_t piece_size = fs.piece_length();

    std::int64_t start_piece = piece_start_offset / piece_size;
    std::int64_t end_piece = (piece_start_offset + size - 1) / piece_size;

    std::vector<lt::piece_index_t> pieces;
    for (lt::piece_index_t i(start_piece); i <= lt::piece_index_t(end_piece); ++i) {
        pieces.emplace_back(i);
    }
    return pieces;
}

std::pair<boost::shared_array<char>, int> 
    media_base::get_piece_at_offset(std::int64_t offset) const {

    if (!m_set_handle) return {
        nullptr, -1
    };

    // receiving piece (unlikely) would mess up iterator finding
    if (m_awaiting_pieces.size()) return {
        nullptr, -1
    };

    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();

    std::int64_t file_offset_in_torrent = fs.file_offset(get_file_index());
    std::int64_t file_size_in_torrent = fs.file_size(get_file_index());

    TORRENT_ASSERT(offset < file_size_in_torrent);
    std::int64_t piece_size = fs.piece_length();
    std::int64_t start_piece = offset / piece_size;

    auto it = m_piece_data.find(lt::piece_index_t(start_piece));
    if (it == m_piece_data.end()) return {
        nullptr, -1
    };
    return it->second;
}

void media_base::process() {
    if (!m_set_handle) return;

    // still has job to do
    if (m_awaiting_pieces.size()) {
        check_last_receive_with_now();
        return;
    }
                                          
    // or big switch table instead
    if (m_mode == media_action_mode::INIT) {
        m_mode = media_action_mode::READY;
    }
    else if (m_mode == media_action_mode::READY) {
        process_ready();
    }
    else if (m_mode == media_action_mode::CAN_FINISH) {
        m_mode = media_action_mode::TRY_TO_FINISH;
        process_final();
    }
    else if (m_mode == media_action_mode::TRY_TO_FINISH) {
        // nothing
    }
    else if (m_mode == media_action_mode::FINISH) {
        // nothing 
    }
    else if (m_mode == media_action_mode::FAIL) {
        // nothing
    }
}

void media_base::request_awaiting_pieces() {
    for (lt::piece_index_t i: m_awaiting_pieces) {
        get_torrent_handle().read_piece(i);
    }
    update_last_receive_to_now();
}

bool media_base::is_finish() const {
    if (!m_set_handle) return false;
    if (m_mode == media_action_mode::FINISH) return true;
    else if (m_mode == media_action_mode::FAIL) return true;
    return false;
}
void media_base::process_final_ready() {
    if (!m_set_handle) return;
    m_mode = media_action_mode::CAN_FINISH;
}
void media_base::process_final_retry() {
    if (!m_set_handle) return;
    m_mode = media_action_mode::CAN_FINISH;
}
void media_base::process_final_fail() {
    if (!m_set_handle) return;
    m_mode = media_action_mode::FAIL;
}
void media_base::process_final_success() {
    if (!m_set_handle) return;
    m_mode = media_action_mode::FINISH;
}

