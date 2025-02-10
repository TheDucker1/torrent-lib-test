#include<libtorrent/libtorrent.hpp>
#include"media_base.hpp"


#ifndef MEDIA_TORRENT_MANAGER_H_
#define MEDIA_TORRENT_MANAGER_H_

struct media_torrent_manager { 
    media_torrent_manager(lt::session_params const&);

    void add_torrent_download(std::string const&);
    void handle_loop();

private:
    void handle_torrent_add(lt::torrent_handle const& h);
    void handle_file_add(
        lt::torrent_handle const& h,
        lt::file_index_t const index,
        std::string const& fn
    );
    void handle_piece_receive(
        lt::torrent_handle const&,
        lt::piece_index_t const,
        boost::shared_array<char> const piece_buffer,
        int const piece_size);

    bool is_support_media(std::string const& sv) const;
    lt::session m_session;
    std::string m_save_path = "./";

    std::list<std::unique_ptr<media_base>> m_media_list;
    
};

#endif // MEDIA_TORRENT_MANAGER_H_
