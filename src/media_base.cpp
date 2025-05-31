#include"media_base.hpp"
#include<atomic>

media_base::media_base(
    lt::torrent_handle const handle,
    lt::file_index_t const file_index
) : m_set_handle(true),
    m_last_receive(std::chrono::steady_clock::now()),
    m_mode(media_action_mode::ACTIVE),
    m_handle(handle),
    m_file_index(file_index)
{
    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();

    m_file_offset_in_torrent = fs.file_offset(get_file_index());
    m_file_size_in_torrent = fs.file_size(get_file_index());

}

void media_base::force_fail() {
    process_final_fail();
}

void media_base::update_last_receive_to_now() {
    if (!m_set_handle) return;
    if (is_busy()) return;
    if (is_finish()) return;
    m_last_receive = std::chrono::steady_clock::now();
}
void media_base::check_last_receive_with_now() {
    if (!m_set_handle) return;
    if (is_busy()) return;
    if (is_finish()) return;
    auto current_clock = std::chrono::steady_clock::now();
    if ((current_clock - m_last_receive) > std::chrono::seconds{15}) {
        request_awaiting_pieces();
    }
}

void media_base::receive_piece(
        lt::piece_index_t const piece_index,
        boost::shared_array<char> const buf_ptr,
        int const buf_size) {
    if (!m_set_handle) return;
    if (is_busy()) return;

    auto it1 = m_piece_data.find(piece_index);
    if (it1 == m_piece_data.end()) m_piece_data[piece_index] = std::make_pair(buf_ptr, buf_size);

    for (auto it = m_awaiting_pieces.find(piece_index); it != m_awaiting_pieces.end();
            it = m_awaiting_pieces.find(piece_index)) {
        it->second(piece_index);
        m_awaiting_pieces.erase(it);
    }

    update_last_receive_to_now();
}

void media_base::set_receive_piece(lt::piece_index_t piece_index, on_piece_specific_callback_type cb) {
    if (!m_set_handle) return;
    if (is_finish()) return;

    m_awaiting_pieces.emplace(piece_index, cb);
    get_torrent_handle().piece_priority(piece_index, lt::default_priority);
    update_last_receive_to_now();
}
void media_base::set_receive_pieces(std::vector<lt::piece_index_t> const _pieces_list,
        on_multi_pieces_callback_type cb) {
    struct Wrapper{
        std::atomic<int> received_cnt;
        int expected_cnt;
        std::vector<lt::piece_index_t> pieces_list;

        Wrapper(std::vector<lt::piece_index_t> const& _pieces_list) {
            pieces_list = _pieces_list;
            received_cnt = 0;
            expected_cnt = static_cast<int>(pieces_list.size());
        }
    };
    auto state = std::make_shared<Wrapper>(_pieces_list); 
    for (lt::piece_index_t piece_index: state->pieces_list) {
        set_receive_piece(piece_index, [state, cb](lt::piece_index_t _) {
            int prev = state->received_cnt.fetch_add(1);
            if (prev+1 == state->expected_cnt) {
                cb(state->pieces_list);
            }
        });
    }
}

std::vector<lt::piece_index_t> 
    media_base::map_byte_range(std::int64_t offset, std::int64_t size) const {
    if (!m_set_handle) return {};
    if (size <= 0) return {};

    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();

    TORRENT_ASSERT(file_size_in_torrent() <= offset + size);
    std::int64_t piece_start_offset = file_offset_in_torrent() + offset;
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

    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();

    TORRENT_ASSERT(offset < file_size_in_torrent());
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
    if (m_mode == media_action_mode::ACTIVE) {
        process_active();
        check_last_receive_with_now();
        return;
    }
    else if (m_mode == media_action_mode::CAN_FINISH) {
        m_mode = media_action_mode::TRY_TO_FINISH;
        process_final();
        return;
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
    if (!m_set_handle) return;
    if (is_busy()) return;
    if (is_finish()) return;
    for (const auto [i, cb]: m_awaiting_pieces) {

        // only request when we have it
        if (get_torrent_handle().have_piece(i))
            get_torrent_handle().read_piece(i);
    }
    update_last_receive_to_now();
}

// not accepting any data writing
bool media_base::is_busy() const {
    if (!m_set_handle) return true;
    if (m_mode == media_action_mode::CAN_FINISH) return true;
    if (m_mode == media_action_mode::TRY_TO_FINISH) return true;
    if (m_mode == media_action_mode::FINISH) return true;
    if (m_mode == media_action_mode::FAIL) return true;
    return false;
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

