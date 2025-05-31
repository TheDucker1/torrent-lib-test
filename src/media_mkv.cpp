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

#include<boost/asio.hpp>

#include"media_send_socket.hpp"

namespace lodepng {
    #include"lodepng.h"
    #include"lodepng.cpp"
}

namespace libav {
    extern "C"{
        #include<libavutil/mem.h>
        #include<libavcodec/avcodec.h>
        #include<libavutil/imgutils.h>
        #include<libswscale/swscale.h>
    }
}; // namespace libav

#include<fstream>

using namespace libebml;
using namespace libmatroska;

std::vector<std::pair<std::string, enum libav::AVCodecID>> const avcodec_list = {
    {"V_AV1"            , libav::AV_CODEC_ID_AV1},
    {"V_AVS2"           , libav::AV_CODEC_ID_AVS2},
    {"V_AVS3"           , libav::AV_CODEC_ID_AVS3},
    {"V_DIRAC"          , libav::AV_CODEC_ID_DIRAC},
    {"V_FFV1"           , libav::AV_CODEC_ID_FFV1},
    {"V_MJPEG"          , libav::AV_CODEC_ID_MJPEG},
    {"V_MPEG1"          , libav::AV_CODEC_ID_MPEG1VIDEO},
    {"V_MPEG2"          , libav::AV_CODEC_ID_MPEG2VIDEO},
    {"V_MPEG4/ISO/ASP"  , libav::AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AP"   , libav::AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/SP"   , libav::AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AVC"  , libav::AV_CODEC_ID_H264},
    {"V_MPEGH/ISO/HEVC" , libav::AV_CODEC_ID_HEVC},
    {"V_MPEGI/ISO/VVC"  , libav::AV_CODEC_ID_VVC},
    {"V_MPEG4/MS/V3"    , libav::AV_CODEC_ID_MSMPEG4V3},
    {"V_PRORES"         , libav::AV_CODEC_ID_PRORES},
    {"V_REAL/RV10"      , libav::AV_CODEC_ID_RV10},
    {"V_REAL/RV20"      , libav::AV_CODEC_ID_RV20},
    {"V_REAL/RV30"      , libav::AV_CODEC_ID_RV30},
    {"V_REAL/RV40"      , libav::AV_CODEC_ID_RV40},
    {"V_SNOW"           , libav::AV_CODEC_ID_SNOW},
    {"V_THEORA"         , libav::AV_CODEC_ID_THEORA},
    {"V_UNCOMPRESSED"   , libav::AV_CODEC_ID_RAWVIDEO},
    {"V_VP8"            , libav::AV_CODEC_ID_VP8},
    {"V_VP9"            , libav::AV_CODEC_ID_VP9}
};
std::string libav_err(int errnum) {
    static char buf[128];
    buf[127] = 0;
    libav::av_strerror(errnum, buf, 128);
    return std::string(buf);
}

enum libav::AVCodecID find_avcodec_by_string(std::string const& name) {
    for (auto const& it: avcodec_list) {
        if (it.first == name)
            return it.second;
    }
    return libav::AV_CODEC_ID_NONE;
}


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
    auto write_file = [&](std::string fn, 
            char * data, std::size_t sz) {
        std::fstream f(fn, std::ios::out | std::ios::binary);
        if (f.fail()) {
            std::cerr << "ERROR WRITING TO " << fn << "\n";
            return;
        }

        f.write(data, sz);
    };
    auto thread_job = [&]()->void{
        std::cerr << "[ START GENERATING THUMBNAIL ]\n";
        std::vector<std::uint8_t> imageData(
            m_codec_helper.pixel_width * m_codec_helper.pixel_height * 3, '\0'
        );

        // std::cerr << "[ Set up libav context ]\n";
        int averror_ret;
        enum libav::AVCodecID codecId = find_avcodec_by_string(m_codec_helper.codec_id);
        if (codecId == libav::AV_CODEC_ID_NONE) {
            std::cerr << "[SERIOUS ERROR]: No suitable codec found" << std::endl;
            return process_final_fail();
        }
        libav::AVCodec const * const codec = libav::avcodec_find_decoder(codecId);

        std::unique_ptr<libav::AVCodecContext, decltype([]
            (libav::AVCodecContext *ctx)->void{
                libav::avcodec_free_context(&ctx);
        })> ctx(
            libav::avcodec_alloc_context3(NULL)
        );
        if (ctx == nullptr) {
            std::cerr << "[SERIOUS ERROR]: Cannot alloc context" << std::endl;
            return process_final_fail();
        }

        std::unique_ptr<libav::AVCodecParameters, decltype([]
            (libav::AVCodecParameters* params) {
            libav::avcodec_parameters_free(&params);
        })> params(
            libav::avcodec_parameters_alloc()
        );
        if (params == nullptr) {
            std::cerr << "[SERIOUS ERROR]: Cannot alloc ctx params" << std::endl;
            return process_final_fail();
        }

        if (m_codec_helper.codec_private.size()) {
            params->extradata_size = m_codec_helper.codec_private.size();
            params->extradata = (uint8_t*)libav::av_malloc(params->extradata_size);
            std::memcpy(params->extradata, m_codec_helper.codec_private.data(),
                    m_codec_helper.codec_private.size());
        }

        if ((averror_ret = libav::avcodec_parameters_to_context(ctx.get(), params.get())) < 0) {
            std::cerr << "[SERIOUS ERROR]: Cannot apply ctx params" << std::endl;
            std::cerr << libav_err(averror_ret) << "\n";
            return process_final_fail();
        }

        if ((averror_ret = libav::avcodec_open2(ctx.get(), codec, NULL)) < 0) {
            std::cerr << "[SERIOUS ERROR]: Cannot open ctx codec" << std::endl;
            std::cerr << libav_err(averror_ret) << "\n";
            return process_final_fail();
        }

        ctx->width = m_codec_helper.pixel_width;
        ctx->height = m_codec_helper.pixel_height;

        std::unique_ptr<libav::AVFrame, decltype([](libav::AVFrame* p) {
            libav::av_frame_free(&p);
        })> frame_video(
            libav::av_frame_alloc()
        );
        if (frame_video == nullptr) {
            std::cerr << "[SERIOUS ERROR]: Cannot alloc video frame" << std::endl;
            return process_final_fail();
        }

        std::unique_ptr<libav::AVFrame, decltype([](libav::AVFrame* p) {
            libav::av_frame_free(&p);
        })> frame_rgb(
            libav::av_frame_alloc()
        );
        if (frame_rgb == nullptr) {
            std::cerr << "[SERIOUS ERROR]: Cannot alloc rgb frame" << std::endl;
            return process_final_fail();
        }

        std::unique_ptr<libav::AVPacket, decltype([](libav::AVPacket* p) {
            libav::av_packet_free(&p);
        })> pkt(
            libav::av_packet_alloc()
        );

        if (pkt == nullptr) {
            std::cerr << "[SERIOUS ERROR]: Cannot alloc packet" << std::endl;
            return process_final_fail();
        }

        frame_rgb->width = m_codec_helper.pixel_width / m_grid_size;
        frame_rgb->height = m_codec_helper.pixel_height / m_grid_size;
        frame_rgb->format = libav::AV_PIX_FMT_RGB24;
        libav::av_image_alloc(
            frame_rgb->data, frame_rgb->linesize,
            frame_rgb->width, frame_rgb->height,
            (enum libav::AVPixelFormat)frame_rgb->format, 1
        );

        for (int i = 0; i < m_cluster_need_size; ++i) {
            // std::cerr << "[ CLUSTER " << (i+1) << " ]\n";

            cluster_helper const& _cluster = m_cluster_list[i];
            m_stream.I_O().setFilePointer(_cluster.m_cluster_pos.first,
                    seek_mode::seek_beginning);
            std::unique_ptr<KaxCluster> cluster(
                static_cast<KaxCluster*>(m_stream.FindNextID(EBML_INFO(KaxCluster), -1))
            );
            if (cluster == nullptr) {
                std::cerr << "[SERIOUS ERROR]: Cannot read cluster\n";
                continue;
            }
            cluster->ReadData(m_stream.I_O(), SCOPE_ALL_DATA);

            KaxSimpleBlock* block = FindChild<KaxSimpleBlock>(*cluster);
            if (block == nullptr) {
                std::cerr << "[ CLUSTER " << (i+1) << " ]: Found no SimpleBlock\n";
                continue;
            }

            if (block->TrackNum() != m_codec_helper.track_number) {
                std::cerr << "[ CLUSTER " << (i+1) << " ]: Wrong track num\n";
                continue;
            }

            int error_in_loop = false;
            do {
                pkt->data = block->GetBuffer(0).Buffer();
                pkt->size = block->GetBuffer(0).Size();
                pkt->flags = AV_PKT_FLAG_KEY; // this is define so no namespace

                if ((averror_ret = 
                            libav::avcodec_send_packet(ctx.get(), pkt.get())) < 0) {
                    std::cerr << "[ CLUSTER " << (i+1) << " ]: Cannot send packet\n";
                    std::cerr << libav_err(averror_ret) << "\n";
                    error_in_loop = true;
                    break;
                }
                if ((averror_ret = 
                            libav::avcodec_receive_frame(ctx.get(), frame_video.get())) < 0) {
                    if (averror_ret != AVERROR(EAGAIN)) {
                        std::cerr << "[ CLUSTER " << (i+1) << " ]: Cannot receive frame\n";
                        std::cerr << " EAGAIN " << (averror_ret == AVERROR(EAGAIN)) << "\n";
                        std::cerr << libav_err(averror_ret) << "\n";
                        error_in_loop = true;
                        break;
                    }
                }
                KaxSimpleBlock* block = FindNextChild<KaxSimpleBlock>(
                        *cluster, *block);
            } while (block != nullptr && averror_ret == AVERROR(EAGAIN));
            if (error_in_loop) {
                std::cerr << "[ CLUSTER " << (i+1) << " ]: Cannot get frame from simple block(s)\n";
                continue;
            }

            std::unique_ptr<struct libav::SwsContext, 
                decltype([](struct libav::SwsContext* p) {
                libav::sws_freeContext(p);
            })> sws_ctx(
                libav::sws_getContext(frame_video->width, frame_video->height,
                    (libav::AVPixelFormat)frame_video->format,
                    frame_rgb->width, frame_rgb->height,
                    (libav::AVPixelFormat)frame_rgb->format,
                    SWS_LANCZOS | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND,
                    NULL, NULL, NULL
                )
            );
            sws_scale(sws_ctx.get(), frame_video->data, frame_video->linesize,
                0, frame_video->height,
                frame_rgb->data, frame_rgb->linesize);

            int const oh = i / m_grid_size;
            int const ow = i % m_grid_size;
            int const linesize = frame_rgb->linesize[0];
            int const linebigsize = m_codec_helper.pixel_width * 3; // rgb24
            std::uint8_t* frame_src = (std::uint8_t*)frame_rgb->data[0];
            std::uint8_t* frame_dst = imageData.data() + 
                oh * frame_rgb->height * linebigsize + 
                ow * linesize;
            for (int hi = 0; hi < frame_rgb->height; ++hi) {
                std::memcpy(frame_dst, frame_src, linesize);
                frame_src += linesize;
                frame_dst += linebigsize;
            }
        }

        unsigned char * png_buf; std::size_t png_size;
        int encode_ret = lodepng::lodepng_encode24(
            &png_buf, &png_size,
            imageData.data(),
            m_codec_helper.pixel_width, m_codec_helper.pixel_height
        );
        if (encode_ret) {
            std::cerr << "ENCODE FAIL\n";
            std::cerr << "ERROR CODE: " << encode_ret << "\n";
            return process_final_fail();
        }
        std::unique_ptr<unsigned char, decltype([](unsigned char* p){
            free(p);
        })> png_ptr(png_buf);

        lt::file_storage const& fs = get_torrent_handle().torrent_file()->files();
        lt::string_view const m_filename = fs.file_name(get_file_index());

        if (!write_data_to_activation_socket(m_filename, png_ptr.get(), png_size)) {
            return process_final_fail();
        }

        std::cerr << "[ FINISH GENERATING THUMBNAIL ]\n";

        return process_final_success();
    };
    std::thread t(thread_job);
    t.detach();
}

void media_mkv::process_active() {
    if (m_cur_mode == mode::READY) {
        m_cur_mode = mode::IGNORE;
        m_search_length = 1024*1024;
        set_receive_pieces(map_byte_range(0, m_search_length), [&](
                std::vector<lt::piece_index_t> const& vt
            ) {
            if (!handle_find_seekhead()) {
                extend_search_head();
                m_cur_mode = mode::READY;
            }
        });
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
}

bool media_mkv::handle_find_seekhead() {
    //theoretically, there could be multiple [head/segment], but we parse the first one only
    if (!m_found_segment) { // find segment first, since everything are offset to segment
        m_stream.I_O().setFilePointer(0, seek_mode::seek_beginning);
        std::unique_ptr<EbmlElement> ebmlHead(
            m_stream.FindNextID(EBML_INFO(EbmlHead), -1)
        );

        if (ebmlHead == nullptr) {
            std::cerr << "[SERIOUS ERROR]: NOT AN EBML DOCUMENT" << std::endl;
            process_final_fail();
            return false;
        }
        ebmlHead->SkipData(m_stream, EBML_CLASS_CONTEXT(EbmlHead));

        std::unique_ptr<EbmlElement> kaxSegment(
            m_stream.FindNextID(EBML_INFO(KaxSegment), -1)
        );
        if (kaxSegment == nullptr) {
            // we presearch 1MB, so must found
            std::cerr << "[SERIOUS ERROR]: NOT AN EBML DOCUMENT" << std::endl;
            process_final_fail();
            return false;
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
            extend_search_head();
            return false;
        }
        m_seekhead_pos = std::make_pair(
                kaxSeekHead->GetElementPosition(),
                m_stream.I_O().getFilePointer()
        );

        set_receive_pieces(map_byte_range(m_seekhead_pos.second, kaxSeekHead->GetSize()), [&]
                (std::vector<lt::piece_index_t> const& _) {handle_get_seekhead();});
    }
    return true;
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

    set_receive_pieces(cue_first, [&](std::vector<lt::piece_index_t> const& _) {handle_cue_and_track_head();});
}

void media_mkv::handle_cue_and_track_head() {
    //handle cue
    m_stream.I_O().setFilePointer(m_cue_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<EbmlElement> cues(
        m_stream.FindNextID(EBML_INFO(KaxCues), -1)
    );
    if (cues == nullptr) {
        std::cerr << "[SERIOUS ERROR]: Cannot read cue\n";
        return process_final_fail();
    }
    m_cue_pos.second = m_stream.I_O().getFilePointer();
    
    std::vector<lt::piece_index_t> cue_second = map_byte_range(m_cue_pos.second, cues->GetSize());
    
    //handle track
    m_stream.I_O().setFilePointer(m_track_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<EbmlElement> tracks(
        m_stream.FindNextID(EBML_INFO(KaxTracks), -1)
    );
    if (tracks == nullptr) {
        std::cerr << "[SERIOUS ERROR]: Cannot read tracks\n";
        return process_final_fail();
    }
    m_track_pos.second = m_stream.I_O().getFilePointer();

    std::vector<lt::piece_index_t> track_second = map_byte_range(m_track_pos.second, tracks->GetSize());

    cue_second.insert(cue_second.end(), track_second.begin(), track_second.end());

    set_receive_pieces(cue_second, [&](std::vector<lt::piece_index_t>const & _) {handle_cue_and_track_data();});
}

void media_mkv::handle_cue_and_track_data() {
    // handle track (video, width, height, codec)
    m_stream.I_O().setFilePointer(m_track_pos.first, seek_mode::seek_beginning);
    std::unique_ptr<KaxTracks> tracks(
        static_cast<KaxTracks*>(m_stream.FindNextID(EBML_INFO(KaxTracks), -1))
    );
    tracks->ReadData(m_stream.I_O(), SCOPE_ALL_DATA);

    KaxTrackEntry* track_entry = FindChild<KaxTrackEntry>(*tracks);
    if (track_entry==nullptr) {
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
                if (kaxCodecId==nullptr) {
                    std::cerr << "[SERIOUS ERROR]: No codec id" << std::endl;
                    return process_final_fail();
                }
                m_codec_helper.codec_id = kaxCodecId->GetValue();

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
    if (cue_point == nullptr) {
        std::cerr << "[SERIOUS ERROR]: No cue found" << std::endl;
        return process_final_fail();
    }
    for (; cue_point != nullptr; cue_point = FindNextChild<KaxCuePoint>(*cues, *cue_point)) {
        KaxCueTime * cue_time = FindChild<KaxCueTime>(*cue_point);
        if (cue_time == nullptr) continue;
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
    int const end_point = m_cluster_list.size();

    // we go several seconds in and let some out due to fade in / fade out (if any)
    int start_index = 2;
    int end_index = end_point - 3;

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
    m_cluster_processed = 0;

    for (auto & cluster: m_cluster_list) {
        std::vector<lt::piece_index_t> cl =
            map_byte_range(cluster.m_cluster_pos.first, 20);
        //cluster_one.insert(cluster_one.end(), cl.begin(), cl.end());


        set_receive_pieces(cl, [&](std::vector<lt::piece_index_t> const& vt) {
            m_stream.I_O().setFilePointer(cluster.m_cluster_pos.first, seek_mode::seek_beginning);
            std::unique_ptr<EbmlElement> kaxCluster(m_stream.FindNextID(EBML_INFO(KaxCluster), -1));
            if (kaxCluster == nullptr) {
                std::cerr << "[SERIOUS ERROR]: Not Cluster\n";
                return process_final_fail();
            }
            cluster.m_cluster_pos.second = m_stream.I_O().getFilePointer();

            std::vector<lt::piece_index_t> cl2 = map_byte_range(cluster.m_cluster_pos.second, kaxCluster->GetSize());
            set_receive_pieces(cl2, [&](std::vector<lt::piece_index_t> const& vt2) {
                m_cluster_processed += 1; // this call is sequential (should be) so shouldn't cause race cond
                if (m_cluster_processed == m_cluster_need_size) {
                    m_cur_mode = mode::FINAL;
                }
            });
        });
    }

}
