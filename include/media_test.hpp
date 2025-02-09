#include<libtorrent/libtorrent.hpp>
#include"media_base.hpp"

#ifndef MEDIA_TEST_H_
#define MEDIA_TEST_H_

struct media_test : media_base {
    media_test(
        lt::torrent_handle const& handle,
        lt::file_index_t const file_index
    );
    
protected:
    void process_ready() override;
    void process_final() override;
private:
    enum struct mode : std::uint8_t {
        STARTUP = 0,
        FINAL,
    };
    mode m_cur_mode;
    std::vector<lt::piece_index_t> get_init_pieces() const;
    std::vector<lt::piece_index_t> get_data_pieces() const;

};



#endif // MEDIA_TEST_H_
