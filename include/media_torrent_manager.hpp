#include<libtorrent/libtorrent.hpp>
#include"media_base.hpp"


#ifndef MEDIA_TORRENT_MANAGER_H_
#define MEDIA_TORRENT_MANAGER_H_

struct media_torrent_manager { 
    media_torrent_manager(lt::session_params const&);

    void add_torrent_download(std::string const&, int const = 0);
    void handle_loop();

private:
    struct sha1comparator {
        bool operator()(lt::sha1_hash const& a, lt::sha1_hash const& b) const {
            for (int i = 0; i < 20; ++i) { // sha1 hash have 20 bytes
                if (a[i] != b[i]) return a[i] < b[i];
            }
            return false; // a == b --> (a<b) == false 
        }
    };
    struct upload_manager {
        upload_manager(lt::torrent_handle h) : handle(h),
            add_tm(std::chrono::steady_clock::now()) {
                handle.set_flags(lt::torrent_flags::share_mode);
            }
        lt::torrent_handle handle;
        std::chrono::time_point<
            std::chrono::steady_clock> add_tm;
    };

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
    void check_resume_data(lt::torrent_handle const &th);

    lt::session m_session;
    std::string m_save_path = "./";

    std::list<std::unique_ptr<media_base>> m_media_list;
    std::map<lt::sha1_hash, int, sha1comparator> m_file_counter;
    std::set<lt::sha1_hash, sha1comparator> m_pending_update;
    std::list<lt::torrent_handle> m_active_list;
    std::list<upload_manager> m_upload_list;
    std::mutex m_mutex;
    
};

#endif // MEDIA_TORRENT_MANAGER_H_
