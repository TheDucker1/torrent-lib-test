#include<libtorrent/libtorrent.hpp>
#include<utility>
#include<functional>
#include<map>
#include<chrono>

#ifndef MEDIA_BASE_H_
#define MEDIA_BASE_H_

struct media_base {
    media_base(
        lt::torrent_handle const handle,
        lt::file_index_t const file_index
    ); 
    ~media_base() = default;

    void force_fail();
    bool is_finish() const;
    bool is_busy() const;
    void receive_piece(lt::piece_index_t,
        boost::shared_array<char> const buf_ptr,
        int const buf_size);
    void process();
    lt::torrent_handle const& get_torrent_handle() const { return m_handle; }
    lt::file_index_t const get_file_index() const { return m_file_index; }


protected:

    using on_piece_specific_callback_type = std::function<void(
            lt::piece_index_t const 
        )>;
    using on_multi_pieces_callback_type = std::function<void(
            std::vector<lt::piece_index_t> const&
        )>;
    void set_receive_piece(lt::piece_index_t piece_index, on_piece_specific_callback_type cb);
    void set_receive_pieces(std::vector<lt::piece_index_t> const pieces_list, on_multi_pieces_callback_type cb);

    void process_final_ready();
    void process_final_retry();
    void process_final_fail();
    void process_final_success();

    // subclasses handle the gathering data state machine themselves
    virtual void process_active()=0;
    // this should spawn a new thread to process data
    // call process_final_fail to reset mode back to CAN_FINISH
    // call process_final_success to set mode to FINISH
    virtual void process_final()=0;

    void update_last_receive_to_now();
    void check_last_receive_with_now();

    std::vector<lt::piece_index_t> 
        map_byte_range(std::int64_t offset, std::int64_t size) const;
    std::pair<boost::shared_array<char>, int> 
        get_piece_at_offset(std::int64_t offset) const;


    std::int64_t file_offset_in_torrent() const { return m_file_offset_in_torrent; }
    std::int64_t file_size_in_torrent() const { return m_file_size_in_torrent; }

    void request_awaiting_pieces();

private:
    enum class media_action_mode : std::uint8_t {
        ACTIVE,            // gathering data, for whatever reason
        CAN_FINISH,        // immediate, for loop handle repeat call finish
        TRY_TO_FINISH,        // immediate, for loop handle repeat call finish
        FINISH,            // done, safe to delete now 
        FAIL               // for some reason, the job fail, safe to delete now
    };

    bool m_set_handle;
    std::chrono::time_point<std::chrono::steady_clock> m_last_receive;

    media_action_mode m_mode; // current mode
    lt::torrent_handle const m_handle; // handle to torrent
    lt::file_index_t const m_file_index; // which file
    std::multimap<lt::piece_index_t, on_piece_specific_callback_type> m_awaiting_pieces; // trigger the callback when empty
    std::map<lt::piece_index_t, std::pair<boost::shared_array<char>, int>>
        m_piece_data;

    std::int64_t m_file_offset_in_torrent;
    std::int64_t m_file_size_in_torrent;
};

#endif // MEDIA_BASE_H_
