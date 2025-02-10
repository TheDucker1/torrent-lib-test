#include"media_mkv.hpp"

#include<ebml/EbmlStream.h>
#include<ebml/EbmlHead.h>
#include<ebml/EbmlElement.h>

#include<matroska/KaxSegment.h>
#include<matroska/KaxSeekHead.h>
#include<matroska/KaxCues.h>
#include<matroska/KaxCuesData.h>
#include<matroska/KaxTracks.h>
#include<matroska/KaxCluster.h>
#include<matroska/KaxSemantic.h>

#include<libtorrent/libtorrent.hpp>

extern "C"{
    #include<libavcodec/avcodec.h>
    #include<libavutil/imgutils.h>
    #include<libswscale/swscale.h>
}


using namespace libebml;
using namespace libmatroska;

media_mkv::media_mkv(
    lt::torrent_handle const& handle,
    lt::file_index_t const file_index
) : media_base(handle, file_index),
    m_found_segment(false),
    m_found_seekhead(false),
    m_found_track(false),
    m_found_cue(false),
    m_cue_list_available(false),
    m_cur_mode (mode::READY),
    m_io(*this),
    m_stream(m_io)
{};

bool media_mkv::io_helper::load_piece() {
    std::pair<boost::shared_array<char>, int> npiece
        = m_Outer.get_piece_at_offset(m_curpos);
    if (npiece.second == -1) return false;

    m_cur_array = npiece;
    lt::file_storage const& fs = m_Outer.get_torrent_handle().torrent_file()->files();
    std::int64_t file_offset_in_torrent = m_Outer.file_offset_in_torrent();
    std::int64_t piece_length = fs.piece_length();
    m_cur_array_offset = (m_curpos + file_offset_in_torrent)  % piece_length;
    // std::cerr << "[ Curoffset: " << m_cur_array_offset << " ]\n";
    // std::cerr << "[ Curlength: " << npiece.second << " ]\n";
    // std::cerr.flush();
    return true;
}

std::size_t media_mkv::io_helper::read(void *Buffer, std::size_t Size) {
    char * ptr = reinterpret_cast<char*>(Buffer);
    std::int64_t size_to_read = Size;
    std::int64_t size_have_read = 0;
    while (size_to_read) {
        if (m_cur_array.second == -1) {
            if (!load_piece()) {
                break;
            }
        }
        std::int64_t cur_arr_left = m_cur_array.second - m_cur_array_offset;
        if (cur_arr_left <= size_to_read) {
            std::memcpy(ptr, m_cur_array.first.get() + m_cur_array_offset, cur_arr_left);
            ptr += cur_arr_left;
            size_have_read += cur_arr_left;
            size_to_read -= cur_arr_left;
            m_curpos += cur_arr_left;

            m_cur_array.second = -1; // mark as unload
        }
        else {
            std::memcpy(ptr, m_cur_array.first.get() + m_cur_array_offset, size_to_read);
            ptr += size_to_read;
            size_have_read += size_to_read;
            m_cur_array_offset += size_to_read;
            m_curpos += size_to_read;

            size_to_read -= size_to_read;
        }
    }
    // std::cerr << "[SIZE MUST READ: " << Size << "]\n";
    // std::cerr << "[SIZE HAVE READ: " << size_have_read << "]\n";
    return static_cast<std::size_t>(size_have_read);
}


void media_mkv::io_helper::setFilePointer(
    std::int64_t Offset, 
    seek_mode Mode
) {
    m_cur_array.second = -1; // mark as invalid
    m_curpos =
        Mode == seek_mode::seek_beginning ? Offset
        : Mode == seek_mode::seek_end     ? (m_file_size - Offset)
        :                                   (m_curpos + Offset);
    m_curpos = std::max(std::int64_t(0), m_curpos);
}

void media_mkv::process_final() {
    std::cerr << "[SUCCESS AAAAAAAAAAAAA]\n";
    auto thread_job = [&]()->void{
        std::cerr << "START SLEEP\n";
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::cerr << "DONE SLEEP\n";
    };
    std::thread t(thread_job);
    t.detach();
    return process_final_success();
}

void media_mkv::process_ready() {
    std::cerr << "[PROCESS]\n";
    if (m_cur_mode == mode::READY) {
        m_search_length = 1024*1024;
        m_cur_mode = mode::FINDING_SEEKHEAD;
        set_receive_pieces(map_byte_range(0, m_search_length));
    }
    else if (m_cur_mode == mode::FINDING_SEEKHEAD) {
        handle_find_seekhead();
    }
    else if (m_cur_mode == mode::GET_SEEKHEAD) {
        handle_get_seekhead();
    }
    else if (m_cur_mode == mode::GET_CUE_AND_TRACK_HEAD) {
        handle_cue_and_track_head();
    }
    else if (m_cur_mode == mode::GET_CUE_AND_TRACK_DATA) {
        handle_cue_and_track_data();
    }
    else if (m_cur_mode == mode::GET_CLUSTER_HEAD) {
        handle_cluster_head();
    }
    else if (m_cur_mode == mode::FINAL) {
        return process_final_ready();
    }
}

void media_mkv::extend_search_head() {
    m_search_length += 1024*1024;
    if (m_search_length > m_search_head_limit) {
        return process_final_fail();
    }
    std::cerr << "[CURRENT SEARCH LENGTH]: " << (m_search_length / 1024/1024) << " MB" << std::endl;
    set_receive_pieces(map_byte_range(0, m_search_length));
}

void media_mkv::handle_find_seekhead() {
    //theoretically, there could be multiple [head/segment], but we parse the first one only
    if (!m_found_segment) { // find segment first, since everything are offset to segment
        m_stream.I_O().setFilePointer(0, seek_mode::seek_beginning);
        std::unique_ptr<EbmlElement> ebmlHead(
            m_stream.FindNextID(EBML_INFO(EbmlHead), -1)
        );

        if (ebmlHead == nullptr) {
            std::cerr << "[SERIOUS ERROR]: NOT AN EBML DOCUMENT" << std::endl;
            process_final_fail();
            return;
        }
        ebmlHead->SkipData(m_stream, EBML_CLASS_CONTEXT(EbmlHead));

        std::unique_ptr<EbmlElement> kaxSegment(
            m_stream.FindNextID(EBML_INFO(KaxSegment), -1)
        );
        if (kaxSegment == nullptr) {
            // we presearch 1MB, so must found
            std::cerr << "[SERIOUS ERROR]: NOT AN EBML DOCUMENT" << std::endl;
            process_final_fail();
            return;
        }
        m_segment_pos = std::make_pair(
                kaxSegment->GetElementPosition(),
                m_stream.I_O().getFilePointer()
        );
        m_found_segment = true;
    }

    if (!m_found_seekhead) {
        m_stream.I_O().setFilePointer(m_segment_pos.second, seek_mode::seek_beginning);

        int upper_elm = -1;
        std::unique_ptr<EbmlElement> kaxSeekHead(
            m_stream.FindNextElement(EBML_CLASS_CONTEXT(KaxSeekHead), upper_elm, -1, false, -1)
        );
        if (kaxSeekHead == nullptr) {
            return extend_search_head();
        }
        m_seekhead_pos = std::make_pair(
                kaxSeekHead->GetElementPosition(),
                m_stream.I_O().getFilePointer()
        );

        m_cur_mode = mode::GET_SEEKHEAD;
        set_receive_pieces(map_byte_range(m_seekhead_pos.second, kaxSeekHead->GetSize()));
    }
}

void media_mkv::handle_get_seekhead() {
    m_stream.I_O().setFilePointer(m_seekhead_pos.first, seek_mode::seek_beginning);
    int upper_elm = -1;
    std::unique_ptr<KaxSeekHead> kaxSeekHead(
        static_cast<KaxSeekHead*>(
            m_stream.FindNextElement(EBML_CLASS_CONTEXT(KaxSeekHead), upper_elm, -1, 0, -1)
        )
    );

    auto data_read = kaxSeekHead->ReadData(m_stream.I_O(), SCOPE_ALL_DATA);
    if (data_read == INVALID_FILEPOS_T) { // bad read
        return process_final_fail();
    }

    KaxSeek* seek = FindChild<KaxSeek>(*kaxSeekHead);
    if (seek == nullptr) {
        std::cerr << "[SERIOUS ERROR]: Found no fast seek" << std::endl;
        return process_final_fail();
    }
    while (seek != nullptr) {
        KaxSeekID* _Id = FindChild<KaxSeekID>(*seek);

        auto const Id = EbmlId(EbmlId::FromBuffer(_Id->GetBuffer(), _Id->GetSize()));
        if (Id == EBML_ID(KaxCues)) {
            if (m_found_cue) {
                std::cerr << "[NOT IMPLEMENT]: Found multiple cues" << std::endl;
                return process_final_fail();
            }
            else {
                m_found_cue = true;
                m_cue_pos.first = seek->Location() + m_segment_pos.second;
            }
        }
        else if (Id == EBML_ID(KaxTracks)) {
            if (m_found_track) {
                std::cerr << "[NOT IMPLEMENT]: Found multiple tracks" << std::endl;
                return process_final_fail();
            }
            else {
                m_found_track = true;
                m_track_pos.first = seek->Location() + m_segment_pos.second;
            }
        }

        seek = FindNextChild<KaxSeek>(*kaxSeekHead, *seek);
    }

    if (!m_found_cue || !m_found_track) {
        std::cerr << "[SERIOUS ERROR]: Not enough metadata for fast seek" << std::endl;
        return process_final_fail();
    }

    // [ID: 4 byte] + [Len: <= 8 byte] --> 12 bytes, 20 for sure
    std::vector<lt::piece_index_t> cue_first = map_byte_range(m_cue_pos.first, 20);
    std::vector<lt::piece_index_t> track_first = map_byte_range(m_track_pos.first, 20);
    cue_first.insert(cue_first.end(), track_first.begin(), track_first.end());

    set_receive_pieces(cue_first);
    m_cur_mode = mode::GET_CUE_AND_TRACK_HEAD;
}

void media_mkv::handle_cue_and_track_head() {
    //handle cue
    m_stream.I_O().setFilePointer(m_cue_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<EbmlElement> cues(
        m_stream.FindNextID(EBML_INFO(KaxCues), -1)
    );
    m_cue_pos.second = m_stream.I_O().getFilePointer();
    
    std::vector<lt::piece_index_t> cue_second = map_byte_range(m_cue_pos.second, cues->GetSize());
    
    //handle track
    m_stream.I_O().setFilePointer(m_track_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<EbmlElement> tracks(
        m_stream.FindNextID(EBML_INFO(KaxTracks), -1)
    );
    m_track_pos.second = m_stream.I_O().getFilePointer();

    std::vector<lt::piece_index_t> track_second = map_byte_range(m_track_pos.second, tracks->GetSize());

    cue_second.insert(cue_second.end(), track_second.begin(), track_second.end());

    set_receive_pieces(cue_second);
    m_cur_mode = mode::GET_CUE_AND_TRACK_DATA;
}

void media_mkv::handle_cue_and_track_data() {
    // handle track (video, width, height, codec)
    m_stream.I_O().setFilePointer(m_track_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<KaxTracks> tracks(
        static_cast<KaxTracks*>(m_stream.FindNextID(EBML_INFO(KaxTracks), -1))
    );
    tracks->ReadData(m_stream.I_O(), SCOPE_ALL_DATA);

    KaxTrackEntry* track_entry = FindChild<KaxTrackEntry>(*tracks);
    if (!track_entry) {
        std::cerr << "[SERIOUS ERROR]: No track found" << std::endl;
        return process_final_fail();
    }
    bool found_video_track = false;
    while (track_entry) {
        KaxTrackType* track_type = FindChild<KaxTrackType>(*track_entry);
        if (track_type==nullptr) {
            std::cerr << "[ERROR]: Track with no type, skip" << std::endl;
        }
        else {
            if (*track_type == MATROSKA_TRACK_TYPE_VIDEO) {
                if (found_video_track) {
                    std::cerr << "[SERIOUS ERROR]: Found multiple video tracks" << std::endl;
                    return process_final_fail();
                }

                found_video_track = true;
                KaxTrackNumber * kaxTrackNumber = FindChild<KaxTrackNumber>(*track_entry);
                if (kaxTrackNumber==nullptr) {
                    std::cerr << "[SERIOUS ERROR]: No track number" << std::endl;
                    return process_final_fail();
                }
                m_codec_helper.track_number = kaxTrackNumber->GetValue();

                KaxTrackVideo* kaxTrackVideo = FindChild<KaxTrackVideo>(*track_entry);
                if (kaxTrackVideo==nullptr) {
                    std::cerr << "[SERIOUS ERROR]: No track video" << std::endl;
                    return process_final_fail();
                }

                KaxVideoPixelWidth* videoPixelWidth = 
                    FindChild<KaxVideoPixelWidth>(*kaxTrackVideo);
                if (videoPixelWidth==nullptr) {
                    std::cerr << "[SERIOUS ERROR]: No video width" << std::endl;
                    return process_final_fail();
                }
                KaxVideoPixelHeight* videoPixelHeight = 
                    FindChild<KaxVideoPixelHeight>(*kaxTrackVideo);
                if (videoPixelHeight==nullptr) {
                    std::cerr << "[SERIOUS ERROR]: No video heidth" << std::endl;
                    return process_final_fail();
                }

                m_codec_helper.pixel_width = videoPixelWidth->GetValue();
                m_codec_helper.pixel_height = videoPixelHeight->GetValue();

                KaxCodecID* kaxCodecId = FindChild<KaxCodecID>(*track_entry);
                if (!kaxCodecId) {
                    std::cerr << "[SERIOUS ERROR]: No codec id" << std::endl;
                    return process_final_fail();
                }
                m_codec_helper.codec_id = kaxCodecId->GetValue();
                // TODO ff_mkv_codec_tags matroska.h libav conversion

                KaxCodecPrivate* kaxCodecPrivate = FindChild<KaxCodecPrivate>(*track_entry);
                if (kaxCodecPrivate) {
                    m_codec_helper.codec_private.resize(kaxCodecPrivate->GetSize());
                    std::memcpy(m_codec_helper.codec_private.data(), 
                            kaxCodecPrivate->GetBuffer(), kaxCodecPrivate->GetSize()
                    );
                }
            }
        }
        track_entry = FindNextChild<KaxTrackEntry>(*tracks, *track_entry);
    }
    if (!found_video_track) {
        std::cerr << "[SERIOUS ERROR]: Found no video" << std::endl;
        return process_final_fail();
    }

    std::cerr << "Track number: " << m_codec_helper.track_number << std::endl;
    std::cerr << "Pixel width: " << m_codec_helper.pixel_width << std::endl;
    std::cerr << "Pixel height: " << m_codec_helper.pixel_height << std::endl;
    std::cerr << "Codec Id: " << m_codec_helper.codec_id << std::endl;
    std::cerr << "Codec Private (Size): " << m_codec_helper.codec_private.size() << std::endl;

    // TODO cuelist, get cluster, parse data
    // handle cues
    m_cluster_list.clear();
    m_stream.I_O().setFilePointer(m_cue_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<KaxCues> cues(
        static_cast<KaxCues*>(m_stream.FindNextID(EBML_INFO(KaxCues), -1))
    );
    cues->ReadData(m_stream.I_O(), SCOPE_ALL_DATA);

    KaxCuePoint* cue_point = FindChild<KaxCuePoint>(*cues);
    if (!cue_point) {
        std::cerr << "[SERIOUS ERROR]: No cue found" << std::endl;
        return process_final_fail();
    }
    for (; cue_point != nullptr; cue_point = FindNextChild<KaxCuePoint>(*cues, *cue_point)) {
        KaxCueTime * cue_time = FindChild<KaxCueTime>(*cue_point);
        if (!cue_time) continue;
        std::int64_t cue_time_pos = cue_time->GetValue();
        KaxCueTrackPositions* cue_track_pos = FindChild<KaxCueTrackPositions>
            (*cue_point);
        if (cue_track_pos == nullptr) continue;
        KaxCueTrack* cue_track = FindChild<KaxCueTrack>(*cue_track_pos);
        if (cue_track == nullptr) continue;
        if (cue_track->GetValue() != m_codec_helper.track_number) continue;

        KaxCueClusterPosition* cue_cluster_position = 
            FindChild<KaxCueClusterPosition>(*cue_track_pos);
        if (cue_cluster_position == nullptr) continue;
        std::int64_t cue_pos_first = m_segment_pos.second 
            + cue_cluster_position->GetValue();

        m_cluster_list.emplace_back(cue_time_pos, std::make_pair(cue_pos_first, -1));
    }

    std::sort(m_cluster_list.begin(), m_cluster_list.end());
    int start_index = 0;
    int const end_point = m_cluster_list.size();
    int end_index = end_point - 1;
    // first second often have no meaningful frame -> seek after that
    while (start_index < end_point && m_cluster_list[start_index].m_time < 1000)
        ++start_index;

    // final second often fade out -> seek before that
    while (end_index && 
            m_cluster_list[end_point-1].m_time - m_cluster_list[end_index].m_time < 1000)
        --end_index;

    if (end_index - start_index + 1 < m_cluster_need_size) {
        std::cerr << "[SERIOUS ERROR]: Not enough frame" << std::endl;
        return process_final_fail();
    }

    // small cluster -> no floating error
    double step = (double)(end_index - start_index) / (m_cluster_need_size - 1);
    std::vector<cluster_helper> cluster_need;
    for (int i = 0; i < m_cluster_need_size; ++i) {
        int idx = (int)(start_index + step * i);
        cluster_need.emplace_back(m_cluster_list[idx]);
    }
    m_cluster_list.swap(cluster_need);
    if (m_cluster_list.size() != m_cluster_need_size) {
        std::cerr << "[SERIOUS ERROR]: Swap cluster error" << std::endl;
        return process_final_fail();
    }

    std::vector<lt::piece_index_t> cluster_one;

    for (auto const& x: m_cluster_list) {
        std::vector<lt::piece_index_t> cl =
            map_byte_range(x.m_cluster_pos.first, 20);
        cluster_one.insert(cluster_one.end(), cl.begin(), cl.end());
    }

    set_receive_pieces(cluster_one);
    m_cur_mode = mode::GET_CLUSTER_HEAD;
}

void media_mkv::handle_cluster_head() {
    std::vector<lt::piece_index_t> cluster_two;    
    for (auto& cluster: m_cluster_list) {
        m_stream.I_O().setFilePointer(cluster.m_cluster_pos.first, 
                seek_mode::seek_beginning);
        std::unique_ptr<EbmlElement> kaxCluster(
            m_stream.FindNextID(EBML_INFO(KaxCluster), -1)
        );
        cluster.m_cluster_pos.second = m_stream.I_O().getFilePointer();


        std::vector<lt::piece_index_t> cl =
            map_byte_range(cluster.m_cluster_pos.second, kaxCluster->GetSize());

        cluster_two.insert(cluster_two.end(), cl.begin(), cl.end());
    }
    
    set_receive_pieces(cluster_two);
    m_cur_mode = mode::FINAL;
}
