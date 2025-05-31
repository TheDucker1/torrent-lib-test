#include<libtorrent/libtorrent.hpp>
#include"media_base.hpp"


#ifndef MEDIA_TORRENT_MANAGER_H_
#define MEDIA_TORRENT_MANAGER_H_

struct media_torrent_manager { 
private:
    struct sha1comparator {
        bool operator()(lt::sha1_hash const& a, lt::sha1_hash const& b) const {
            for (int i = 0; i < 20; ++i) { // sha1 hash have 20 bytes
                if (a[i] != b[i]) return a[i] < b[i];
            }
            return false; // a == b --> (a<b) == false 
        }
    };
    struct handle_manager {
        handle_manager() : handle(lt::torrent_handle()), last_upd(std::chrono::steady_clock::now()) {}
        handle_manager(lt::torrent_handle h) : handle(h),
            last_upd(std::chrono::steady_clock::now()) {
        }
        void set_download() {
            if (!handle.is_valid()) return;
            handle.unset_flags(lt::torrent_flags::upload_mode);
            is_uploading = 0;
        }
        void set_upload() {
            if (!handle.is_valid()) return;
            if (is_uploading) return;
            is_uploading = 1;
            handle.unset_flags(lt::torrent_flags::auto_managed);
            handle.set_flags(lt::torrent_flags::upload_mode); 
        }
        void update() {
            last_upd = std::chrono::steady_clock::now();
        }
        int is_uploading = 0;
        lt::torrent_handle handle;
        std::chrono::time_point<
            std::chrono::steady_clock
        > last_upd;
    };

    void set_handler_manager(lt::torrent_handle const& h);
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
    handle_manager m_current_handle;
    std::list<std::unique_ptr<media_base>> m_media_list;
    std::list<lt::torrent_handle> m_pending_list;
    std::mutex m_mutex;

public:
    media_torrent_manager(lt::session_params const&);

    void apply_settings (lt::settings_pack const& sp) {return m_session.apply_settings(sp);};
    void add_torrent_download(std::string const&);
    std::chrono::time_point<std::chrono::steady_clock> get_last_upd() {
        return m_current_handle.last_upd;
    }
    void handle_loop();
};

#endif // MEDIA_TORRENT_MANAGER_H_
