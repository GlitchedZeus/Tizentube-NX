#ifdef TTNX_M3_INTEGRATED
#include "m3_player_activity.hpp"
#include "m3_diagnostics.hpp"
#include "app_storage.hpp"
#include "tizentube_nx/library/local_library.hpp"
#include "tizentube_nx/library/playback_resume_gate.hpp"
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#include "tizentube_nx/playback/rolling_reprime_gate.hpp"
#include "tizentube_nx/playback/rolling_epoch_clock.hpp"
#endif
#include "media_file_sink.hpp"
#include "hls_cache_sink.hpp"
#include "tizentube_nx/playback/audio_gate.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#if defined(TTNX_M3_ADAPTIVE_EDL_SEEK)
#include "tizentube_nx/playback/local_adaptive_edl.hpp"
#endif
#ifdef TTNX_M4_TV_PLAYER_UI
#include "tizentube_nx/ui/tv_player_controls.hpp"
#endif
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
#include "rolling_hls_sink.hpp"
#include "tizentube_nx/playback/local_live_hls_gate.hpp"
#endif

#include <borealis.hpp>
#include <mpv/client.h>
#include <mpv/render.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace ttnx::switch_app {
namespace {
constexpr int kWidth=640, kHeight=360;
std::uint64_t playback_clock_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
class CachedMpv {
public:
    explicit CachedMpv(bool hls=false,bool adaptive=false,bool live=false
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        ,std::shared_ptr<playback::RollingStreamEpoch> ring_epoch={}
#endif
    ):
        pixels_(kWidth*kHeight*4,0),use_hls_(hls),
        use_adaptive_(adaptive),use_live_hls_(live)
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        ,ring_epoch_(std::move(ring_epoch))
#endif
    {}
    ~CachedMpv() {
        // Closing libmpv releases every media FD and the exclusive lease.
        // Only now may the app erase the playback session's disposable SD
        // bytes, including video/audio that Replay kept while this view lived.
        close();
        const bool files=SwitchMediaCacheSink::discard_session_media();
        const bool hls=SwitchHlsCacheSink::discard_session_media();
        M3Diagnostics::record(M3Stage::Cache,
            files && hls ? M3Event::Pass : M3Event::Fail,
            6,files ? 1 : 0,hls ? 1 : 0);
    }
    bool init() {
        M3Diagnostics::record(M3Stage::Player,M3Event::Start,use_hls_ ? 2 : (use_adaptive_ ? 3 : 1),0,0);
        if (handle_) return renderer_!=nullptr;
        if(!ready_lease_.acquire()) {
            M3Diagnostics::record(M3Stage::Player,M3Event::Refused,1,0,0);
            status_="Another completed media file is already playing";
            return false;
        }
        handle_=mpv_create();
        if (!handle_) {
            M3Diagnostics::record(M3Stage::Player,M3Event::Fail,2,0,0);
            status_="mpv_create failed";
            ready_lease_.release();
            return false;
        }
        auto required=[this](const char* key,const char* value) {
            const int result=mpv_set_option_string(handle_,key,value);
            // Older Switch libmpv builds omit scripting entirely.
            if (result==MPV_ERROR_OPTION_NOT_FOUND &&
                (!std::strcmp(key,"load-scripts") || !std::strcmp(key,"ytdl")))
                return true;
            if (result>=0) return true;
            M3Diagnostics::record(M3Stage::Player,M3Event::Error,1,result,0);
            status_=std::string("mpv option ")+key+": "+mpv_error_string(result);
            return false;
        };
        if (!required("config","no") ||
            !required("load-scripts","no") ||
            !required("ytdl","no") ||
            !required("input-default-bindings","no") ||
            !required("demuxer-lavf-o",
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
                      ring_epoch_ ? "protocol_whitelist=file,ttnxring" :
#endif
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
                      use_live_hls_
                          ? "protocol_whitelist=file,crypto,data"
                          : "protocol_whitelist=file") ||
#else
                      "protocol_whitelist=file") ||
#endif
            !required("vo","libmpv") ||
            // This is the libmpv CPU-memory software render API. Do not
            // select a hardware-only video surface it cannot upload to
            // NanoVG; prefer dependable H.264 software decode first.
            !required("hwdec","no") ||
            !required("audio-channels","stereo")) {
            close();return false;
        }
        if(use_adaptive_) {
            const bool pair_ready=SwitchMediaCacheSink::adaptive_pair_valid();
            M3Diagnostics::record(M3Stage::Cache,M3Event::Status,3,pair_ready ? 1 : 0,0);
#if defined(TTNX_M3_ADAPTIVE_EDL_SEEK)
            if(!pair_ready) {
#else
            if(!pair_ready ||
               !attach_external_aac(SwitchMediaCacheSink::kAdaptiveAudio)) {
#endif
                status_="Adaptive H.264/AAC files incomplete or unverified";
                close();return false;
            }
        }
        if(use_hls_) {
            // Every atomically published HLS clip declares its audio topology.
            char mode[24]{};
            std::FILE* marker=std::fopen(SwitchHlsCacheSink::kReadyAudioMode,"rb");
            const auto n=marker?std::fread(mode,1,sizeof(mode)-1,marker):0;
            if(marker)std::fclose(marker);
            const bool external=n==9 && std::memcmp(mode,"external\n",9)==0;
            if(!external && !(n==9 && std::memcmp(mode,"playlist\n",9)==0) &&
               !(n==6 && std::memcmp(mode,"muxed\n",6)==0)) {
                status_="Published cache lacks verified audio topology";
                close();return false;
            }
            if(external) {
                std::FILE* audio=std::fopen(SwitchHlsCacheSink::kReadyExternalAac,"rb");
                const bool positioned=audio && std::fseek(audio,0,SEEK_END)==0;
                const long length=positioned?std::ftell(audio):-1;
                if(audio)std::fclose(audio);
                std::FILE* size_file=std::fopen(SwitchHlsCacheSink::kReadyExternalAacBytes,"rb");
                unsigned long long expected=0;
                char terminator=0;
                const bool exact=size_file &&
                    std::fscanf(size_file,"%llu%c",&expected,&terminator)==2 &&
                    terminator=='\n' && std::fgetc(size_file)==EOF;
                if(size_file)std::fclose(size_file);
                if(length<1024 || length>128L*1024*1024 ||
                   !exact || expected!=static_cast<unsigned long long>(length) ||
                   !attach_external_aac(SwitchHlsCacheSink::kReadyExternalAac)) {
                    status_="Required external AAC missing or unsupported by mpv";
                    close();return false;
                }
            }
        }
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        // Switch-NewPipe-style callback MUST be registered before initialize.
        // The player holds exactly one M3 lease; the ring does not acquire a
        // second lease or let libmpv open a network URL.
        if(ring_epoch_ && !ring_epoch_->register_decoder(handle_)) {
            status_="Bounded ring prefix/decoder ownership not ready";
            close();return false;
        }
#endif
        const int init_result=mpv_initialize(handle_);
        if (init_result<0) {
            M3Diagnostics::record(M3Stage::Player,M3Event::Error,2,init_result,0);
            status_=std::string("mpv initialize: ")+mpv_error_string(init_result);
            close();return false;
        }
        mpv_render_param api[]={
            {MPV_RENDER_PARAM_API_TYPE,const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID,nullptr}
        };
        // Capture only numeric categories of upstream libmpv warnings.
        // Never write the untrusted text: it can contain signed media URLs.
        const int watch_result=mpv_request_log_messages(handle_,"warn");
        M3Diagnostics::record(M3Stage::Player,M3Event::Status,
                              3,watch_result,0);
        const int render_result=mpv_render_context_create(&renderer_,handle_,api);
        if (render_result<0) {
            M3Diagnostics::record(M3Stage::Render,M3Event::Fail,1,render_result,0);
            status_=std::string("mpv renderer: ")+mpv_error_string(render_result);
            close();return false;
        }
        // Upstream libmpv requires a registered update callback for
        // correct video-frame delivery. The callback runs on an mpv thread:
        // it ONLY increments an atomic, never touches libmpv or Borealis.
        mpv_render_context_set_update_callback(
            renderer_,&CachedMpv::notify_render_update,this);
        M3Diagnostics::record(M3Stage::Render,M3Event::Ready,1,kWidth,kHeight);
        status_=use_live_hls_?"Ready: reviewed local rolling HLS probe":
                              "Ready: local verified cache";
        return true;
    }
    bool attach_external_aac(const char* path) {
        // No pseudo-option suffixes: use a typed array so sdmc:/ remains
        // ONE literal local path, not a colon-delimited list.
        mpv_node one{};
        one.format=MPV_FORMAT_STRING;
        one.u.string=const_cast<char*>(path);
        mpv_node_list elements{};
        elements.num=1;
        elements.values=&one;
        mpv_node array{};
        array.format=MPV_FORMAT_NODE_ARRAY;
        array.u.list=&elements;
        const int rc=mpv_set_option(handle_,"audio-files",MPV_FORMAT_NODE,&array);
        M3Diagnostics::record(M3Stage::Decode,M3Event::Selected,2,rc,0);
        if(rc<0) {
            status_=std::string("External audio config: ")+mpv_error_string(rc);
            return false;
        }
        return true;
    }
    [[nodiscard]] bool requires_audio_gate()const noexcept {
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_)return true; // muxed H264/AAC must prove selected audio
#endif
        return use_hls_ || use_adaptive_ || use_live_hls_;
    }
    bool play() {
        if (!renderer_ && !init())return false;
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_) {
            if(!ring_epoch_->ring_for_decoder() || ring_loaded_) {
                status_="Ring replay/seek requires a new, verified stream epoch";
                return false;
            }
            const char* cmd[]={"loadfile",playback::RollingMpvStreamBridge::kURI,
                              "replace",nullptr};
            audio_gate_.reset();
            audio_failure_latched_=false;
            have_frame_=false;
            const int result=mpv_command(handle_,cmd);
            M3Diagnostics::record(M3Stage::Player,M3Event::Begin,result,0,0);
            if(result<0){status_="Local ring protocol was rejected";return false;}
            ring_loaded_=true;
            ring_verified_av_=false;
            epoch_frame_base_=frames_;
            if(!ring_clock_.reset(ring_epoch_->epoch_start_ms(),
                                  ring_epoch_->total_duration_ms())) {
                status_="M4 source playlist lacks a validated playback clock";
                return false;
            }
            status_="Opening verified *local* H264/AAC ring stream...";
            return true;
        }
#endif
        const char* path=use_hls_
            ? SwitchHlsCacheSink::kReadyMaster
            : use_adaptive_?SwitchMediaCacheSink::kAdaptiveVideo:
                           SwitchMediaCacheSink::kComplete;
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
        // Fixed app-owned filename ONLY, never a signed media URL.
        if(use_live_hls_)path=RollingHlsSink::kPlaylist;
#endif
        std::FILE* file=std::fopen(path,"rb");
        if (!file) {
            M3Diagnostics::record(M3Stage::Cache,M3Event::Fail,4,0,0);
            status_="Cache missing: retry video from Search";return false;
        }
        bool ok=std::fseek(file,0,SEEK_END)==0;
        const long bytes=ok?std::ftell(file):-1;
        std::fclose(file);
        const long max_size=(use_hls_ || use_live_hls_)
            ?1024L*1024:512L*1024*1024;
        const long minimum=(use_hls_ || use_live_hls_)?20:1024;
        M3Diagnostics::record(M3Stage::Cache,M3Event::Status,use_hls_ ? 2 : (use_adaptive_ ? 3 : 1),bytes,0);
        if (bytes<minimum || bytes>max_size) {
            M3Diagnostics::record(M3Stage::Cache,M3Event::Fail,5,bytes,0);
            status_="Completed local media missing or exceeds bounds";
            return false;
        }
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
        if(use_live_hls_) {
            if(bytes<=0 || bytes>65536) {
                status_="Local rolling playlist is outside fixed bounds";
                return false;
            }
            std::FILE* manifest=std::fopen(path,"rb");
            if(!manifest)return false;
            std::string data(static_cast<std::size_t>(bytes),'\0');
            const auto count=std::fread(data.data(),1,data.size(),manifest);
            const bool closed=std::fclose(manifest)==0;
            playback::LocalLiveHlsGate gate{};
            if(!closed || count!=data.size() ||
               !playback::parse_local_live_hls(data,gate) ||
               !gate.playable_prefix()) {
                status_="Local rolling HLS prefix not safe or ready";
                return false;
            }
            // Probe is intentionally not connected to the app launcher.
            // The caller MUST keep the producer alive and join it before
            // unloading/cleaning this media. Never auto-restart evicted HLS.
            if(live_loaded_) {
                status_="Rolling live probe replay requires new seek epoch";
                return false;
            }
            live_loaded_=true;
        }
#endif
#if defined(TTNX_M3_ADAPTIVE_EDL_SEEK)
        // mpv EDL new_stream puts two already-verified local tracks in
        // one timeline, avoiding the known --audio-files fast-seek bug.
        std::string edl;
        if(use_adaptive_) {
            auto combined=playback::local_adaptive_edl(
                SwitchMediaCacheSink::kAdaptiveVideo,
                SwitchMediaCacheSink::kAdaptiveAudio);
            if(!combined){status_="Local A/V EDL rejected";return false;}
            edl=*combined;
            path=edl.c_str();
        }
#endif
        const char* cmd[]={"loadfile",path,"replace",nullptr};
        audio_gate_.reset();
        audio_failure_latched_=false;
        have_frame_=false;
        const int load_rc=mpv_command(handle_,cmd);
        M3Diagnostics::record(M3Stage::Player,M3Event::Begin,load_rc,0,0);
        if(load_rc<0) {
            status_="mpv rejected completed cache";return false;
        }
        status_=use_live_hls_?"Opening reviewed *local* growing HLS...":
            use_hls_?"Opening fully cached HLS + AAC playlists...":
            use_adaptive_?"Opening video MP4 plus verified AAC file...":
                          "Loading completed local MP4...";
        return true;
    }
    void pause() {
        if(!handle_)return;
        const char* cmd[]={"cycle","pause",nullptr};
        const int result=mpv_command(handle_,cmd);
        M3Diagnostics::record(M3Stage::Player,M3Event::Pause,result,0,0);
    }
    void seek(int sec) {
        if(!handle_)return;
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_) {
            status_="Ring seek requires decoder-close and a new stream epoch";
            return;
        }
#endif
        if(use_live_hls_) {
            // Seeking outside the retained HLS window requires the producer
            // to stop, re-prime and create a new epoch before opening mpv.
            status_="Live seek awaits verified re-prime; no unsafe mpv seek";
            return;
        }
        const auto arg=std::to_string(sec);
#if defined(TTNX_M3_ADAPTIVE_EDL_SEEK)
        // Exact, not mpv's keyframe-only default relative seek.
        const char* mode=use_adaptive_?"relative+exact":"relative";
#else
        const char* mode="relative";
#endif
        const char* cmd[]={"seek",arg.c_str(),mode,nullptr};
        const int result=mpv_command(handle_,cmd);
        M3Diagnostics::record(M3Stage::Player,M3Event::Seek,sec,result,0);
    }
    void stop() {
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        // A Stop during playback OR while waiting for the next HTTPS prefix
        // closes the decoder, joins the producer and removes app-owned SD.
        if(ring_epoch_) {
            ring_reprime_.cancel();
            if(handle_) {
                const char* stop_command[]={"stop",nullptr};
                const int result=mpv_command(handle_,stop_command);
                M3Diagnostics::record(M3Stage::Player,M3Event::Stop,result,0,0);
            }
            close();
            audio_gate_.reset();
            audio_failure_latched_=false;
            have_frame_=false;
            const bool deleted=ring_epoch_->snapshot().phase==
                playback::RollingStreamEpoch::Phase::Cleaned;
            ring_terminal_retired_=deleted;
            M3Diagnostics::record(M3Stage::Cache,M3Event::Status,
                                  33,deleted?1:0,0);
            status_=deleted
                ?"Stopped; disposable M4 media deleted after decoder close"
                :"Stopped; media deletion pending safe reader release";
            return;
        }
#endif
        if(!handle_)return;
        const char* cmd[]={"stop",nullptr};
        const int result=mpv_command(handle_,cmd);
        M3Diagnostics::record(M3Stage::Player,M3Event::Stop,result,0,0);
        audio_gate_.reset();
        audio_failure_latched_=false;
        have_frame_=false;
        status_="Stopped. A replays completed cache.";
    }
    [[nodiscard]] bool seek_absolute(std::uint32_t seconds) {
        if(!handle_ || use_live_hls_ || seconds<5)return false;
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_)return false;
#endif
        const auto text=std::to_string(seconds);
        const char* cmd[]={"seek",text.c_str(),"absolute",nullptr};
        const int result=mpv_command(handle_,cmd);
        M3Diagnostics::record(M3Stage::Player,M3Event::Seek,
                              static_cast<int>(seconds),result,1);
        return result>=0;
    }
    [[nodiscard]] std::uint32_t duration_seconds()const noexcept {
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_)return static_cast<std::uint32_t>(
            ring_epoch_->total_duration_ms()/1000);
#endif
        if(!handle_)return 0;
        double duration=0;
        if(mpv_get_property(handle_,"duration",MPV_FORMAT_DOUBLE,&duration)<0 ||
           !std::isfinite(duration) || duration<0 ||
           duration>static_cast<double>(UINT32_MAX))return 0;
        return static_cast<std::uint32_t>(duration);
    }
    // The audio gate counts bounded active UI time, so sleep/wake cannot
    // instantly trip the missing-AAC timeout after a long suspend.
    // Query the selected audio stream on each active UI tick, rather than
    // assuming the first PLAYBACK_RESTART event has finalized all tracks.
    bool selected_video_track() const {
        if(!handle_)return false;
        std::int64_t count=0;
        if(mpv_get_property(handle_,"track-list/count",MPV_FORMAT_INT64,&count)<0)
            return false;
        for(std::int64_t i=0;i<count && i<32;++i) {
            const auto prefix="track-list/"+std::to_string(i)+"/";
            char* type=mpv_get_property_string(handle_,(prefix+"type").c_str());
            const bool is_video=type && std::strcmp(type,"video")==0;
            if(type)mpv_free(type);
            int selected=0;
            if(is_video && mpv_get_property(handle_,(prefix+"selected").c_str(),
                                            MPV_FORMAT_FLAG,&selected)>=0 &&
               selected)return true;
        }
        return false;
    }
    bool selected_audio_track() const {
        if(!handle_)return false;
        std::int64_t count=0;
        if(mpv_get_property(handle_,"track-list/count",MPV_FORMAT_INT64,&count)<0)
            return false;
        for(std::int64_t track=0;track<count && track<32;++track) {
            const std::string prefix="track-list/"+std::to_string(track)+"/";
            char* type=mpv_get_property_string(handle_,(prefix+"type").c_str());
            const bool audio=type && std::strcmp(type,"audio")==0;
            if(type)mpv_free(type);
            int selected=0;
            if(audio && mpv_get_property(handle_,(prefix+"selected").c_str(),
                                         MPV_FORMAT_FLAG,&selected)>=0 && selected)
                return true;
        }
        return false;
    }
    void pump() {
        if(!handle_)return;
        ++pump_ticks_;
        for(unsigned i=0;i<32;++i) {
            const mpv_event* e=mpv_wait_event(handle_,0);
            if(!e || e->event_id==MPV_EVENT_NONE)break;
            if(e->event_id==MPV_EVENT_LOG_MESSAGE && mpv_warns_<64) {
                const auto* msg=static_cast<const mpv_event_log_message*>(e->data);
                ++mpv_warns_;
                // Log message body/prefix are NEVER recorded. Coarse
                // category and numeric level only, bounded to 64 events.
                const char* prefix=msg ? msg->prefix : nullptr;
                const int category=!prefix?0:
                    std::strstr(prefix,"vd")||std::strstr(prefix,"video")?1:
                    std::strstr(prefix,"vo")||std::strstr(prefix,"render")?2:
                    std::strstr(prefix,"lavf")||std::strstr(prefix,"demux")?3:
                    std::strstr(prefix,"ad")||std::strstr(prefix,"audio")?4:
                    std::strstr(prefix,"ffmpeg")?5:0;
                M3Diagnostics::record(M3Stage::Decode,M3Event::Warning,
                                      category,msg ? msg->log_level : 0,0);
            }
            if(e->event_id==MPV_EVENT_FILE_LOADED) {
                M3Diagnostics::record(M3Stage::Decode,M3Event::Loaded,e->error,0,0);
                if((requires_audio_gate()) && audio_gate_.state()==
                   playback::AudioSelectionGate::State::Idle)
                    audio_gate_.begin(playback_clock_ms());
                status_="Local media loaded; validating selected AAC audio...";
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
                if(ring_epoch_)status_="Ring MPEG-TS loaded; verifying AAC audio...";
                else
#endif
                if(!use_hls_&&!use_adaptive_)status_="Local MP4 loaded";
            }
            if(e->event_id==MPV_EVENT_PLAYBACK_RESTART) {
                M3Diagnostics::record(M3Stage::Decode,M3Event::Restart,e->error,0,0);
                if(requires_audio_gate()) {
                    if(audio_gate_.state()!=
                       playback::AudioSelectionGate::State::Checking)
                        audio_gate_.begin(playback_clock_ms());
                    status_="Verifying video and AAC track selection...";
                } else status_="Playing";
            }
            if(e->event_id==MPV_EVENT_VIDEO_RECONFIG) {
                M3Diagnostics::record(M3Stage::Decode,M3Event::Status,1,e->error,0);
            }
            if(e->event_id==MPV_EVENT_AUDIO_RECONFIG) {
                M3Diagnostics::record(M3Stage::Decode,M3Event::Status,2,e->error,0);
            }
            if(e->event_id==MPV_EVENT_END_FILE) {
                audio_gate_.reset();
                const auto* end=static_cast<const mpv_event_end_file*>(e->data);
                M3Diagnostics::record(M3Stage::Decode,M3Event::End,end ? end->reason : -1,end ? end->error : 0,0);
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
                if(ring_epoch_ && ring_loaded_ &&
                   ring_reprime_.stage()==playback::RollingReprimeGate::Stage::Watching) {
                    using Gate=playback::RollingReprimeGate;
                    const auto snap=ring_epoch_->snapshot();
                    const auto active=ring_epoch_->ring_for_decoder();
                    const auto stats=active?active->stats():
                        playback::RollingFileStream::Stats{};
                    const Gate::Boundary boundary{snap.next_segment_index,
                        snap.epoch_origin_segment,snap.total_segments,
                        snap.phase==playback::RollingStreamEpoch::Phase::Backpressure,
                        snap.decoder_attached,active && stats.complete &&
                        !stats.failed && !stats.cancelled};
                    const auto why=end && end->reason==MPV_END_FILE_REASON_EOF
                        ?Gate::End::Eof:
                        end && end->reason==MPV_END_FILE_REASON_ERROR
                            ?Gate::End::Error:Gate::End::Stop;
                    if(ring_reprime_.observe(why,boundary,
                            ring_verified_av_ && !audio_failure_latched_)) {
                        // Numeric-only telemetry: no signed CDN URL, headers
                        // or private watch data can enter this log.
                        M3Diagnostics::record(M3Stage::Cache,M3Event::Status,
                            30,static_cast<std::int64_t>(snap.next_segment_index),
                            static_cast<std::int64_t>(snap.peak_file_bytes));
                        status_="Verified ring quota reached; closing decoder for continuation";
                    }
                }
#endif
                if(!audio_failure_latched_)
                    status_=(end && end->reason==MPV_END_FILE_REASON_ERROR)
                        ?"Decoder error opening local media":
                         "Video stopped or finished";
            }
        }
        if((requires_audio_gate()) && audio_gate_.state()==
                       playback::AudioSelectionGate::State::Checking) {
            const auto outcome=audio_gate_.observe(
                selected_audio_track(),playback_clock_ms());
            if(outcome==playback::AudioSelectionGate::State::Ready) {
                M3Diagnostics::record(M3Stage::Decode,M3Event::Selected,selected_video_track() ? 1 : 0,selected_audio_track() ? 1 : 0,0);
                status_=use_adaptive_?"Playing: H.264 MP4 with selected AAC":
                                      "Playing: HLS video with selected AAC track";
            } else if(outcome==playback::AudioSelectionGate::State::Missing) {
                M3Diagnostics::record(M3Stage::Decode,M3Event::NoAudio,selected_video_track() ? 1 : 0,0,0);
                audio_failure_latched_=true;
                status_="No selected AAC audio after initialization; playback stopped";
                const char* cmd[]={"stop",nullptr};
                (void)mpv_command(handle_,cmd);
                have_frame_=false;
            }
        }
        // A/V liveness is not the same as audio-only playback. Capture
        // selected tracks, decoded geometry, actual rendered frames and
        // time position independently every ~2 seconds.
        if((pump_ticks_%120)==0) {
            std::int64_t width=0,height=0;
            const int wrc=mpv_get_property(handle_,"video-params/w",
                                           MPV_FORMAT_INT64,&width);
            const int hrc=mpv_get_property(handle_,"video-params/h",
                                           MPV_FORMAT_INT64,&height);
            M3Diagnostics::record(M3Stage::Decode,M3Event::Status,wrc>=0 ? width : 0,hrc>=0 ? height : 0,selected_video_track() ? 1 : 0);
            double time=0;
            const bool have_time=mpv_get_property(handle_,"time-pos",
                                          MPV_FORMAT_DOUBLE,&time)>=0;
            M3Diagnostics::record(M3Stage::Player,M3Event::Progress,have_time ? static_cast<std::int64_t>(time*1000) : -1,selected_audio_track() ? 1 : 0,static_cast<std::int64_t>(frames_));
            if(selected_audio_track() && !selected_video_track())
                M3Diagnostics::record(M3Stage::Decode,M3Event::NoVideo,0,1,static_cast<std::int64_t>(frames_));
        }
    }
    const std::vector<unsigned char>& render() {
        // Run from the main UI tick, independently of whether Borealis
        // happened to redraw the video widget this frame. Never clear
        // pending texture state here: the last decoded image is retained
        // until the widget actually uploads it to NanoVG.
        if(!renderer_)return pixels_;
        const auto notifications=render_notifications_.exchange(
            0,std::memory_order_acq_rel);
        const auto flags=mpv_render_context_update(renderer_);
        ++render_calls_;
        if(render_calls_%120==0)
            M3Diagnostics::record(M3Stage::Render,M3Event::Status,
                static_cast<std::int64_t>(flags),
                static_cast<std::int64_t>(notifications),
                static_cast<std::int64_t>(frames_));
        if(flags & MPV_RENDER_UPDATE_FRAME) {
            int size[2]{kWidth,kHeight};
            std::size_t stride=kWidth*4;
            mpv_render_param params[]={
                {MPV_RENDER_PARAM_SW_SIZE,size},
                {MPV_RENDER_PARAM_SW_FORMAT,const_cast<char*>("rgb0")},
                {MPV_RENDER_PARAM_SW_STRIDE,&stride},
                {MPV_RENDER_PARAM_SW_POINTER,pixels_.data()},
                {MPV_RENDER_PARAM_INVALID,nullptr}
            };
            const int result=mpv_render_context_render(renderer_,params);
            if(result>=0) {
                for(std::size_t i=3;i<pixels_.size();i+=4)pixels_[i]=255;
                have_frame_=true;new_frame_=true;++frames_;
                // A successful render call can still paint entirely black.
                // Sample a small grid; never write any image pixels to SD.
                unsigned bright=0;
                for(std::size_t i=0;i<pixels_.size();i+=65536) {
                    if(pixels_[i] || pixels_[i+1] || pixels_[i+2])++bright;
                }
                if(frames_<=3 || frames_%120==0)
                    M3Diagnostics::record(M3Stage::Render,M3Event::Frame,static_cast<std::int64_t>(frames_),bright,static_cast<std::int64_t>(flags));
                mpv_render_context_report_swap(renderer_);
            } else {
                M3Diagnostics::record(M3Stage::Render,M3Event::Error,result,static_cast<std::int64_t>(flags),0);
                status_=std::string("Render: ")+mpv_error_string(result);
            }
        }
        return pixels_;
    }
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
    void update_ring_buffer_window() noexcept {
        if(!ring_epoch_ || !ring_loaded_ ||
           frames_<=epoch_frame_base_ || !have_frame_ ||
           !selected_video_track() || !selected_audio_track())return;
        // Only a decoded frame from THIS decoder and selected AAC can
        // advance its producer. Earlier epochs never certify a new epoch.
        double raw=0;
        if(mpv_get_property(handle_,"time-pos",MPV_FORMAT_DOUBLE,&raw)<0 ||
           !ring_clock_.observe(raw))return;
        ring_verified_av_=true;
        ring_epoch_->advance_playback_ms(ring_clock_.relative_ms());
    }
    // M4 experimental callback only: drive teardown and fresh prefix on a
    // UI tick, never inside a libmpv callback or the network worker thread.
    void service_ring_reprime() {
        if(!ring_epoch_)return;
        using Gate=playback::RollingReprimeGate;
        const auto stage=ring_reprime_.stage();
        if(stage==Gate::Stage::Finished || stage==Gate::Stage::Failed) {
            // No watched video survives EOF or a decoder error. This is
            // separate from user Stop, which retires media immediately.
            if(!ring_terminal_retired_) {
                close();
                const bool deleted=ring_epoch_->snapshot().phase==
                    playback::RollingStreamEpoch::Phase::Cleaned;
                ring_terminal_retired_=deleted;
                status_=!deleted
                    ?"M4 media cleanup pending safe reader release"
                    :stage==Gate::Stage::Finished
                        ?"M4 playback complete; temporary video deleted"
                        :"M4 playback failed; temporary media deleted";
            }
            return;
        }
        if(stage==Gate::Stage::Requested) {
            const auto next=ring_reprime_.next_segment();
            // close destroys the render context and mpv, joins the worker,
            // verifies zero outstanding decoder readers and unlinks old SD.
            close();
            if(ring_epoch_->snapshot().phase!=
                   playback::RollingStreamEpoch::Phase::Cleaned ||
               !ring_epoch_->restart_at(next) || !ring_epoch_->start() ||
               !ring_reprime_.started(next)) {
                ring_reprime_.fail();
                ring_epoch_->request_stop();
                status_="M4 continuation blocked: decoder/cache not safely retired";
                return;
            }
            ring_loaded_=false;
            ring_verified_av_=false;
            epoch_frame_base_=frames_;
            M3Diagnostics::record(M3Stage::Cache,M3Event::Status,
                                  31,static_cast<std::int64_t>(next),0);
            status_="M4 continuation: preparing next verified SD prefix...";
        }
        if(ring_reprime_.stage()!=Gate::Stage::WaitingForPrefix)return;
        const auto s=ring_epoch_->snapshot();
        if(s.phase==playback::RollingStreamEpoch::Phase::Failed ||
           s.phase==playback::RollingStreamEpoch::Phase::StopRequested ||
           s.phase==playback::RollingStreamEpoch::Phase::Joined ||
           s.phase==playback::RollingStreamEpoch::Phase::Cleaned ||
           (s.phase==playback::RollingStreamEpoch::Phase::Backpressure &&
            !s.segments_committed)) {
            ring_reprime_.fail();
            ring_epoch_->request_stop();
            status_="M4 continuation failed before verified media was available";
            return;
        }
        if(!ring_epoch_->ring_for_decoder())return; // no UI polling/blocking
        if(!init() || !play() || !ring_reprime_.prefix_ready()) {
            ring_reprime_.fail();
            close();
            status_="M4 continuation: decoder restart failed";
            return;
        }
        M3Diagnostics::record(M3Stage::Decode,M3Event::Status,
                              32,static_cast<std::int64_t>(s.segments_committed),
                              static_cast<std::int64_t>(s.peak_file_bytes));
        status_="M4 continuation opened; verifying current video and AAC...";
    }
#endif
    [[nodiscard]] std::uint32_t playback_seconds()const noexcept {
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_)return ring_clock_.anchored()
            ?static_cast<std::uint32_t>(ring_clock_.absolute_ms()/1000):0;
#endif
        if(!handle_)return 0;
        double time=0;
        if(mpv_get_property(handle_,"time-pos",MPV_FORMAT_DOUBLE,&time)<0 ||
           !std::isfinite(time) || time<0 ||
           time>static_cast<double>(UINT32_MAX))return 0;
        return static_cast<std::uint32_t>(time);
    }
    [[nodiscard]] bool is_paused()const noexcept {
        if(!handle_)return false;
        int paused=0;
        return mpv_get_property(handle_,"pause",MPV_FORMAT_FLAG,&paused)>=0
            && paused!=0;
    }
    bool have_frame()const{return have_frame_;}
    std::uint64_t frames_rendered()const noexcept{return frames_;}
    bool new_frame()const{return new_frame_;}
    const std::vector<unsigned char>& pixels()const{return pixels_;}
    void acknowledge_frame_upload() noexcept {new_frame_=false;}
    const std::string& status()const{return status_;}
    std::string stats()const{
        if(!renderer_)return "Decoder unavailable";
        const char* h=mpv_get_property_string(handle_,"hwdec-current");
        const char* v=mpv_get_property_string(handle_,"video-codec");
        const char* a=mpv_get_property_string(handle_,"audio-codec");
        double seconds=0;
        const bool time=mpv_get_property(handle_,"time-pos",MPV_FORMAT_DOUBLE,&seconds)>=0;
        auto str=std::string("V ")+(selected_video_track()?"yes":"no")+
            " A "+(selected_audio_track()?"yes":"no")+
            " | HW "+(h?h:"unknown")+
            " | "+(v?v:"--")+" / "+(a?a:"--")+
            " | "+(time?std::to_string(static_cast<int>(seconds)):"--")+"s"+
            " | frames "+std::to_string(frames_);
        if(h)mpv_free(const_cast<char*>(h));
        if(v)mpv_free(const_cast<char*>(v));
        if(a)mpv_free(const_cast<char*>(a));
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_) {
            const auto s=ring_epoch_->snapshot();
            // Only numeric, bounded diagnostics. Signed CDN URLs and
            // response headers must NEVER reach UI logs or files.
            str+=" | M4 "+std::to_string(s.peak_file_bytes/1024)+
                 " KiB "+std::to_string(s.segments_committed)+" seg";
            if(s.waiting_for_playback)str+=" wait";
        }
#endif
        return str;
    }
private:
    static void notify_render_update(void* context) noexcept {
        if(context)static_cast<CachedMpv*>(context)->
            render_notifications_.fetch_add(1,std::memory_order_release);
    }
    void close() {
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_)ring_epoch_->request_stop();
#endif
        if(renderer_){
            mpv_render_context_set_update_callback(renderer_,nullptr,nullptr);
            mpv_render_context_free(renderer_);renderer_=nullptr;
        }
        if(handle_){mpv_terminate_destroy(handle_);handle_=nullptr;}
        // Release ONLY after libmpv has closed every file descriptor.
        ready_lease_.release();
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(ring_epoch_) {
            // This may block on a bounded SSL timeout: never call from draw.
            // The optional activity is unavailable from production menus.
            ring_epoch_->stop_and_join();
            (void)ring_epoch_->notify_decoder_destroyed();
            (void)ring_epoch_->cleanup();
        }
#endif
        M3Diagnostics::record(M3Stage::Player,M3Event::End,static_cast<std::int64_t>(frames_),0,0);
    }
    mpv_handle* handle_{nullptr};
    mpv_render_context* renderer_{nullptr};
    std::vector<unsigned char> pixels_;
    bool use_hls_{false},use_adaptive_{false},use_live_hls_{false};
    bool live_loaded_{false};
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
    std::shared_ptr<playback::RollingStreamEpoch> ring_epoch_{};
    playback::RollingReprimeGate ring_reprime_{};
    playback::RollingEpochClock ring_clock_{};
    bool ring_loaded_{false},ring_verified_av_{false};
    bool ring_terminal_retired_{false};
    std::uint64_t epoch_frame_base_{0};
#endif
    std::string status_{"Starting libmpv..."};
    bool have_frame_{false},new_frame_{false};
    playback::AudioSelectionGate audio_gate_{};
    playback::HlsPlaybackLease ready_lease_{};
    bool audio_failure_latched_{false};
    unsigned long long frames_{0};
    std::uint64_t pump_ticks_{0},render_calls_{0};
    std::atomic<std::uint64_t> render_notifications_{0};
    unsigned mpv_warns_{0};
};
class VideoFrame final:public brls::View {
public:
    explicit VideoFrame(CachedMpv& player
#ifdef TTNX_M4_TV_PLAYER_UI
                        ,ttnx::ui::TvPlayerControls* controls=nullptr
#endif
                        ):player_(player)
#ifdef TTNX_M4_TV_PLAYER_UI
                        ,controls_(controls)
#endif
    {
#ifdef TTNX_M4_TV_PLAYER_UI
        setHeight(602);
#else
        setHeight(294);
#endif
    }
    ~VideoFrame() override {
        // NanoVG image handles are owned by the active UI video context.
        // Free them while Borealis still has an active video backend, not
        // when the next video opens (which would leak GPU memory on replay).
        if(image_>0)nvgDeleteImage(brls::Application::getNVGContext(),image_);
    }
    [[nodiscard]] std::uint64_t draw_count()const noexcept{return draw_calls_;}
    void draw(NVGcontext* vg,float x,float y,float w,float h,
              brls::Style,brls::FrameContext*) override {
        ++draw_calls_;
        const auto& pixels=player_.pixels();
        if(image_<=0) {
            image_=nvgCreateImageRGBA(vg,kWidth,kHeight,0,pixels.data());
            if(image_>0)player_.acknowledge_frame_upload();
            if(draw_calls_<=3 || draw_calls_%120==0)
                M3Diagnostics::record(M3Stage::Ui,
                    image_>0?M3Event::Ready:M3Event::Fail,1,image_,0);
        } else if(player_.new_frame()) {
            nvgUpdateImage(vg,image_,pixels.data());
            player_.acknowledge_frame_upload();
            ++texture_updates_;
            if(texture_updates_<=3 || texture_updates_%120==0)
                M3Diagnostics::record(M3Stage::Ui,M3Event::Presented,
                    static_cast<std::int64_t>(texture_updates_),image_,1);
        }
        nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,8);
        nvgFillColor(vg,nvgRGB(8,14,20));nvgFill(vg);
        if(image_>0 && player_.have_frame()) {
            const float scale=std::min(w/kWidth,h/kHeight);
            const float vw=kWidth*scale,vh=kHeight*scale;
            const float vx=x+(w-vw)*0.5f,vy=y+(h-vh)*0.5f;
            nvgBeginPath(vg);nvgRect(vg,vx,vy,vw,vh);
            nvgFillPaint(vg,nvgImagePattern(vg,vx,vy,vw,vh,0,image_,1));
            nvgFill(vg);
        }
#ifdef TTNX_M4_TV_PLAYER_UI
        if(controls_ && controls_->transport_visible() && player_.have_frame()) {
            // Original, low-overhead NanoVG TV transport. This overlays
            // the same accepted video texture; no second decoder, frame
            // allocation, thumbnail download or SD artifact is created.
            const float inset=std::max(24.0f,0.045f*w);
            const float rail_x=x+inset;
            const float rail_w=std::max(0.0f,w-2.0f*inset);
            const float rail_y=y+h-95.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg,x,y+h-143.0f,w,143.0f,0.0f);
            nvgFillColor(vg,nvgRGBA(0,0,0,156));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRoundedRect(vg,rail_x,rail_y,rail_w,5.0f,2.5f);
            nvgFillColor(vg,nvgRGBA(255,255,255,105));
            nvgFill(vg);
            const float progress=ttnx::ui::TvPlayerControls::progress(
                static_cast<double>(player_.playback_seconds()),
                static_cast<double>(player_.duration_seconds()));
            if(progress>0.0f) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg,rail_x,rail_y,rail_w*progress,5.0f,2.5f);
                nvgFillColor(vg,nvgRGBA(255,255,255,245));
                nvgFill(vg);
            }
            const float mid_x=x+w*0.5f,mid_y=y+h-47.0f;
            nvgBeginPath(vg);
            nvgCircle(vg,mid_x,mid_y,27.0f);
            nvgFillColor(vg,nvgRGBA(255,255,255,224));
            nvgFill(vg);
            nvgBeginPath(vg);
            if(player_.is_paused()) {
                nvgMoveTo(vg,mid_x-6,mid_y-10);
                nvgLineTo(vg,mid_x+11,mid_y);
                nvgLineTo(vg,mid_x-6,mid_y+10);
                nvgClosePath(vg);
            } else {
                nvgRect(vg,mid_x-8,mid_y-10,6,20);
                nvgRect(vg,mid_x+3,mid_y-10,6,20);
            }
            nvgFillColor(vg,nvgRGB(23,23,23));
            nvgFill(vg);
        }
#endif
        // The user accepted real visible video on Switch. Leave the
        // production picture unobstructed; retain numeric frame/texture
        // diagnostics for future faults instead of RGB calibration bars.
        if(draw_calls_<=2 || draw_calls_%120==0)
            M3Diagnostics::record(M3Stage::Ui,M3Event::Draw,
                static_cast<std::int64_t>(draw_calls_),
                player_.have_frame()?1:0,image_>0?1:0);
    }
private:
    CachedMpv& player_;int image_{-1};
#ifdef TTNX_M4_TV_PLAYER_UI
    ttnx::ui::TvPlayerControls* controls_{nullptr}; // borrowed UI state
#endif
    std::uint64_t draw_calls_{0},texture_updates_{0};
};
brls::Label* label(brls::Box* box,const std::string& s,int size) {
    auto* v=new brls::Label();
    v->setText(s);v->setFontSize(size);v->setMarginBottom(5);box->addView(v);
    return v;
}
void add_button(brls::Box* row,const char* title,std::function<void()> action) {
    auto* b=new brls::Button();
    b->setText(title);b->setGrow(1.0f);b->setMarginRight(8);b->setHeight(52);
    b->registerClickAction([fn=std::move(action)](brls::View*) {
        fn();return true;
    });
    row->addView(b);
}
class M3Activity final:public brls::Activity {
public:
    explicit M3Activity(bool hls,bool adaptive=false,bool live=false,
                        std::string video_id={},
                        std::uint32_t resume_seconds=0
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
                        ,std::shared_ptr<playback::RollingStreamEpoch> ring_epoch={}
#endif
                        ):
        player_(hls,adaptive,live
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
                ,std::move(ring_epoch)
#endif
                ),hls_(hls),adaptive_(adaptive),
        live_(live),video_id_(std::move(video_id)),
        resume_gate_(resume_seconds){active_=this;}
    ~M3Activity() override {
        persist_history();
        active_=nullptr;
        player_.stop();
    }
    brls::View* createContentView() override {
        auto* root=new brls::Box(brls::Axis::COLUMN);
#ifdef TTNX_M4_TV_PLAYER_UI
        root->setPadding(4,8,4,8);
        video_=new VideoFrame(player_,&tv_controls_);
        root->addView(video_);
        auto* actions=new brls::Box(brls::Axis::ROW);
        actions->setHeight(54);
        root->addView(actions);
        add_button(actions,"Replay",[this]{
            player_.play();tv_controls_.activate(playback_clock_ms());update();
        });
        add_button(actions,"Pause / Resume",[this]{
            player_.pause();tv_controls_.activate(playback_clock_ms());update();
        });
        add_button(actions,"-10s",[this]{
            player_.seek(-10);tv_controls_.activate(playback_clock_ms());update();
        });
        add_button(actions,"+10s",[this]{
            player_.seek(10);tv_controls_.activate(playback_clock_ms());update();
        });
        add_button(actions,"Details",[this]{
            tv_controls_.diagnostics(playback_clock_ms());update();
        });
        add_button(actions,"Exit",[this]{
            persist_history();player_.stop();brls::Application::popActivity();
        });
        status_=label(root,player_.status(),14);
        info_=label(root,player_.stats(),14);
        status_->setVisibility(brls::Visibility::GONE);
        info_->setVisibility(brls::Visibility::GONE);
#else
        root->setPadding(12,20,12,20);
        label(root,live_?"TizenTube NX | M4 Local Live HLS Probe":
            hls_?"TizenTube NX | M3 HLS Playback":
                   adaptive_?"TizenTube NX | Adaptive H.264 + AAC":
                             "TizenTube NX | M3 Normal Video",24);
        label(root,live_?"Opt-in local-only HLS; hardware decode not accepted":
            hls_?"Completed offline HLS segments + synchronized AAC":
                   adaptive_?"Verified local video MP4 and separate AAC":
                             "Self-contained H.264/AAC MP4 from verified SD cache",15);
        label(root,M3Diagnostics::available()
            ?"Diagnostics: sdmc:/switch/TizenTube-NX/m3-playback.log"
            :"Diagnostics: SD logging unavailable",12);
        // Keep a borrowed pointer for an independent UI-presentation watchdog.
        // Ownership stays with Borealis inside the activity content tree.
        video_=new VideoFrame(player_);
        root->addView(video_);
        status_=label(root,player_.status(),17);
        info_=label(root,player_.stats(),15);
        auto* one=new brls::Box(brls::Axis::ROW);one->setHeight(56);root->addView(one);
        add_button(one,"Replay (A)",[this]{player_.play();update();});
        add_button(one,"Pause / Resume",[this]{player_.pause();update();});
        add_button(one,"Stop (X)",[this]{
            persist_history();player_.stop();update();
        });
        auto* two=new brls::Box(brls::Axis::ROW);two->setHeight(56);root->addView(two);
        add_button(two,"Seek -10s (L)",[this]{player_.seek(-10);update();});
        add_button(two,"Seek +10s (R)",[this]{player_.seek(10);update();});
#endif
        root->registerAction("Back",brls::BUTTON_B,[this](brls::View*) {
#ifdef TTNX_M4_TV_PLAYER_UI
            if(tv_controls_.close_one(playback_clock_ms())) {
                update();return true;
            }
#endif
            persist_history();player_.stop();brls::Application::popActivity();return true;
        });
        root->registerAction("Pause",brls::BUTTON_Y,[this](brls::View*) {
            player_.pause();
#ifdef TTNX_M4_TV_PLAYER_UI
            tv_controls_.activate(playback_clock_ms());
#endif
            update();return true;
        });
#ifdef TTNX_M4_TV_PLAYER_UI
        root->registerAction("Controls",brls::BUTTON_X,[this](brls::View*) {
            tv_controls_.toggle(playback_clock_ms());update();return true;
        });
#else
        root->registerAction("Stop",brls::BUTTON_X,[this](brls::View*) {
            persist_history();player_.stop();update();return true;
        });
#endif
        root->registerAction("Seek -10s",brls::BUTTON_LB,[this](brls::View*) {
            player_.seek(-10);
#ifdef TTNX_M4_TV_PLAYER_UI
            tv_controls_.activate(playback_clock_ms());
#endif
            update();return true;
        });
        root->registerAction("Seek +10s",brls::BUTTON_RB,[this](brls::View*) {
            player_.seek(10);
#ifdef TTNX_M4_TV_PLAYER_UI
            tv_controls_.activate(playback_clock_ms());
#endif
            update();return true;
        });
        // CRITICAL pinned Borealis (20e2d33b) lifecycle contract:
        // pushActivity() sets the incoming opaque root alpha to 0 during the
        // old activity's fade-out, then calls show(). View::show() immediately
        // returns WITHOUT restoring alpha when !hidden. A newly constructed
        // Box is initially !hidden, so video/audio run behind an invisible
        // activity until B triggers hide(), resets alpha to 1 and briefly
        // reveals the decoded image. Explicitly prime hidden before push.
        // The framework's show() now performs the real 0 -> 1 fade-in.
        root->hide([]{},false,0.0f);
        player_.init();
        player_.play();
        update();
        return root;
    }
    void tick() {
        player_.pump();
        // Software frame dequeue must not depend on widget draw invalidation:
        // audio can advance even when Borealis does not redraw this View.
        (void)player_.render();
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
        // After actual picture and selected audio, wake the producer to
        // fetch at most 15s ahead of the real decoder clock.
        player_.update_ring_buffer_window();
        // Experimental M4 only; normal accepted M3 never owns a ring epoch.
        player_.service_ring_reprime();
#endif
#ifdef TTNX_M4_TV_PLAYER_UI
        tv_controls_.tick(playback_clock_ms(),player_.is_paused());
#endif
        // Record only *actually rendered* normal videos with selected audio,
        // not failed downloads, loadfile errors, or Shorts. No signed URLs
        // or thumbnails are ever persisted. Writes are spaced to 30s or exit.
        std::uint32_t resume_target=0;
        if(resume_gate_.offer(player_.have_frame(),player_.selected_audio_track(),
                              live_,player_.duration_seconds(),resume_target))
            resume_gate_.acknowledge(player_.seek_absolute(resume_target));
        if(!video_id_.empty() &&
           library::LocalLibrary::valid_id(video_id_) &&
           player_.frames_rendered()>0 && player_.selected_audio_track() &&
           resume_gate_.can_checkpoint(player_.playback_seconds())) {
            const auto current=player_.playback_seconds();
            if(!history_started_) {
                history_started_=true;
                persist_history();
            } else if(current>=last_persisted_resume_+30 &&
                      playback_clock_ms()-last_history_write_ms_>=30000) {
                persist_history();
            }
        }
        // An audio-only-looking black screen is now distinguishable from
        // actual decode failure. Pinned Borealis draws active views on each
        // frame; 90 decoded non-black frames without a SINGLE widget draw
        // means presentation/activity lifecycle is broken, not media data.
        if(!presentation_warning_latched_ && player_.frames_rendered()>=90 &&
           video_ && video_->draw_count()==0) {
            presentation_warning_latched_=true;
            auto* root=getContentView();
            const auto alpha=root?static_cast<std::int64_t>(
                root->getAlpha()*1000):-1;
            M3Diagnostics::record(M3Stage::Ui,M3Event::Fail,2,
                static_cast<std::int64_t>(player_.frames_rendered()),alpha);
        }
        if((++ticks_%45)==0)update();
    }
    static void tick_current(){if(active_)active_->tick();}
private:
    void persist_history() {
        if(!history_started_ || !library::LocalLibrary::valid_id(video_id_))
            return;
        const auto seconds=player_.playback_seconds();
        const auto now=playback_clock_ms();
        // On stop, libmpv can clear time-pos before Borealis destroys the
        // activity. Never replace a real resume position with that missing
        // property interpreted as zero. An explicit fresh seek is handled
        // by playback's periodic positive-time checkpoint instead.
        if(history_persisted_ && seconds==0 && last_persisted_resume_>0)return;
        if(history_persisted_ && seconds==last_persisted_resume_)return;
        const std::string file=std::string(ttnx::kAppDirectory)+"/library.txt";
        library::LocalLibrary local;
        (void)local.load(file);
        const std::time_t wall=std::time(nullptr);
        local.record_watch(video_id_,wall>0?static_cast<std::uint64_t>(wall):0,
                           seconds);
        if(local.save(file)) {
            history_persisted_=true;
            last_persisted_resume_=seconds;
            last_history_write_ms_=now;
        } else {
            M3Diagnostics::record(M3Stage::Cache,M3Event::Fail,8,0,0);
        }
    }
    void update(){
        if(status_)status_->setText(player_.status());
        if(info_)info_->setText(player_.stats());
#ifdef TTNX_M4_TV_PLAYER_UI
        const bool detail=tv_controls_.panel()==
            ttnx::ui::TvPlayerControls::Panel::Diagnostics;
        if(status_)status_->setVisibility(detail?
            brls::Visibility::VISIBLE:brls::Visibility::GONE);
        if(info_)info_->setVisibility(detail?
            brls::Visibility::VISIBLE:brls::Visibility::GONE);
#endif
    }
    static M3Activity* active_;
    CachedMpv player_;
    bool hls_{false},adaptive_{false},live_{false};
    std::string video_id_{};
    library::PlaybackResumeGate resume_gate_{};
    bool history_started_{false},history_persisted_{false};
    std::uint32_t last_persisted_resume_{0};
    std::uint64_t last_history_write_ms_{0};
    bool presentation_warning_latched_{false};
#ifdef TTNX_M4_TV_PLAYER_UI
    ttnx::ui::TvPlayerControls tv_controls_{};
#endif
    VideoFrame* video_{nullptr}; // borrowed, destroyed by the activity tree
    brls::Label* status_{nullptr};
    brls::Label* info_{nullptr};
    unsigned ticks_{0};
};
M3Activity* M3Activity::active_=nullptr;
} // namespace

void open_m3_completed_media(const std::string& video_id,
                             std::uint32_t resume_seconds){
    brls::Application::pushActivity(new M3Activity(false,false,false,video_id,resume_seconds));
}
void open_m3_completed_hls(const std::string& video_id,
                           std::uint32_t resume_seconds){
    brls::Application::pushActivity(new M3Activity(true,false,false,video_id,resume_seconds));
}
void open_m3_completed_adaptive(const std::string& video_id,
                                std::uint32_t resume_seconds){
    brls::Application::pushActivity(new M3Activity(false,true,false,video_id,resume_seconds));
}
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
// This symbol intentionally has no main-menu entry. Integration must
// prove producer-join + decoder-close ordering before enabling access.
void open_m4_local_hls_probe(){
    brls::Application::pushActivity(new M3Activity(false,false,true));
}
#endif
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
void open_m4_native_ring_candidate(std::shared_ptr<playback::RollingStreamEpoch> epoch) {
    // Never expose a half-written stream or a URL to libmpv.
    if(!epoch || !epoch->ring_for_decoder())return;
    brls::Application::pushActivity(new M3Activity(false,false,false,{},0,
                                                  std::move(epoch)));
}
#endif
void tick_m3_player(){M3Activity::tick_current();}
} // namespace ttnx::switch_app
#endif // TTNX_M3_INTEGRATED
