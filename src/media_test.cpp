#include"media_test.hpp"

media_test::media_test(
    lt::torrent_handle const& handle,
    lt::file_index_t const file_index
) : media_base(handle, file_index),
    m_cur_mode(mode::STARTUP)
{};


std::vector<lt::piece_index_t> 
    media_test::get_init_pieces() const {

    // get first 1MB
    return map_byte_range(0, 1*1024*1024);
}
std::vector<lt::piece_index_t> media_test::get_data_pieces() const {
    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();
    std::int64_t file_size_in_torrent = fs.file_size(get_file_index());

    std::int64_t start_offset = file_size_in_torrent / 10 * 4; // ~ 40%
    std::int64_t get_size = file_size_in_torrent / 20; // ~ 5%

    return map_byte_range(start_offset, get_size);

};

void media_test::process_ready() {
    if (m_cur_mode == media_test::mode::STARTUP) {
        set_receive_pieces(get_init_pieces());
        m_cur_mode = media_test::mode::FINAL;
    }
    else {
        set_receive_pieces(get_data_pieces());
        process_final_ready();
        
    }
}

void media_test::process_final() {
    std::pair<boost::shared_array<char>, int> piece_data;

    piece_data = get_piece_at_offset(0);
    if (piece_data.second == -1) {
        std::cerr << "[PROCESS FAIL]: not found first piece" << std::endl;
        process_final_fail();
        return;
    }

    lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();
    std::int64_t file_offset_in_torrent = fs.file_offset(get_file_index());
    std::int64_t file_size_in_torrent = fs.file_size(get_file_index());
    std::int64_t piece_size = fs.piece_length();

    std::int64_t start_offset = file_size_in_torrent / 10 * 4; // ~ 40%
    std::int64_t get_size = file_size_in_torrent / 20; // ~ 5%

    std::vector<lt::piece_index_t> pieces = map_byte_range(start_offset, get_size);
    for (lt::piece_index_t const idx: pieces) {
        std::int64_t idx_int = static_cast<std::int64_t>(idx);    
        std::int64_t piece_offset_relative_to_file = idx_int * piece_size - file_offset_in_torrent;
        piece_offset_relative_to_file = std::max(piece_offset_relative_to_file,
                std::int64_t(0));
        piece_data = get_piece_at_offset(piece_offset_relative_to_file);
        if (piece_data.second == -1) {
            std::cerr << "[PROCESS FAIL]: not found .44 piece" << std::endl;
            process_final_fail();
            return;
        }

    }

    std::cerr << "[PROCESS SUCCESS]" << std::endl;
    process_final_success();
    return;
}
