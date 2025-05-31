#include"media_base.hpp"
#include<ebml/IOCallback.h>
#include<ebml/EbmlStream.h>

#ifndef MEDIA_MKV_H_
#define MEDIA_MKV_H_

struct media_mkv final : media_base {
    
    media_mkv(
        lt::torrent_handle const& handle,
        lt::file_index_t const file_index
    );


protected:

    void process_active() override;
    void process_final() override;

private:
    bool handle_find_seekhead();
    void handle_get_seekhead();
    void handle_cue_and_track_head();
    void handle_cue_and_track_data();
    void extend_search_head();
    std::int64_t const m_search_head_limit = std::int64_t(10)*1024*1024;

    struct cluster_helper {
        std::int64_t m_time;
        std::pair<std::int64_t, std::int64_t> m_cluster_pos;
        
        cluster_helper(std::int64_t tm, 
                std::pair<std::int64_t, std::int64_t> ps) 
            : m_time(tm), m_cluster_pos(ps) {}
        bool operator<(cluster_helper const& o) const { return m_time < o.m_time; }
    };
    struct codec_helper {
        std::uint64_t track_number;
        std::uint64_t pixel_width;
        std::uint64_t pixel_height;
        std::string codec_id;
        std::vector<char> codec_private;
    } m_codec_helper;


    struct io_helper : public libebml::IOCallback {
        io_helper(media_mkv const& Outer) : 
            m_Outer(Outer), m_curpos(0),
            m_cur_array(std::make_pair(nullptr, -1)),
            m_cur_array_offset(0),

            m_file_size(Outer.get_torrent_handle()
                    .torrent_file()->files().file_size(Outer.get_file_index()))
        {}
        ~io_helper() override = default;

        std::size_t read(void* Buffer, std::size_t Size) override;
        void setFilePointer(
                std::int64_t Offset, 
                libebml::seek_mode Mode
        ) override;
        std::size_t write(const void* Buffer, std::size_t Size) override { return 0; }
        std::uint64_t getFilePointer() override { return static_cast<std::uint64_t>(m_curpos); }
        void close() override { return; }

        bool load_piece();

        media_mkv const& m_Outer;
        std::int64_t m_curpos;
        std::pair<boost::shared_array<char>, int> m_cur_array;
        int m_cur_array_offset;
        std::int64_t const m_file_size;
    };
    enum struct mode : std::uint8_t {
        READY = 0,              // increasing data fetch to get seekhead
        GET_SEEKHEAD,           // have seekhead, find tracks, cuelist
        GET_CUE_AND_TRACK_HEAD, // first few byte of cue and track to determine the size later
        GET_CUE_AND_TRACK_DATA, // first few byte of cue and track to determine the size later
        GET_CLUSTER_HEAD,       // have all cluster head
        FINAL,                  // have enough data for processing
        IGNORE,
    };

    bool m_cue_list_available;
    bool m_found_segment;
    bool m_found_seekhead;
    bool m_found_track;
    bool m_found_cue;
    mode m_cur_mode;
    io_helper m_io;
    libebml::EbmlStream m_stream;
    std::int64_t m_search_length;
    std::pair<std::int64_t, std::int64_t> m_segment_pos; // everything are offset to segment head
    std::pair<std::int64_t, std::int64_t> m_seekhead_pos;

    //support one track only [for now]
    std::pair<std::int64_t, std::int64_t> m_track_pos;
    std::pair<std::int64_t, std::int64_t> m_cue_pos;
    std::vector<std::pair<std::int64_t, std::int64_t>> m_clusters_pos;

    std::vector<cluster_helper> m_cluster_list;
    int const m_grid_size = 5;
    int const m_cluster_need_size = m_grid_size * m_grid_size;
    int m_cluster_processed = 0;
    // std::mutex mtx; // lock for sequential lock processing (shouldn't need as the call already block?)
};


#endif // MEDIA_MKV_H_
