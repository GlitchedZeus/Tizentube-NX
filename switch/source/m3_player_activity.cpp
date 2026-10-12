#ifdef TTNX_M3_INTEGRATED
#include "m3_player_activity.hpp"
#include "media_file_sink.hpp"
#include "hls_cache_sink.hpp"
#include "tizentube_nx/playback/audio_gate.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"

#include <borealis.hpp>
#include <mpv/client.h>
#include <mpv/render.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
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
    explicit CachedMpv(bool hls=false):pixels_(kWidth*kHeight*4,0),use_hls_(hls){}
    ~CachedMpv() { close(); }
    bool init() {
        if (handle_) return renderer_!=nullptr;
        if(!ready_lease_.acquire()) {
            status_="Another completed media file is already playing";
            return false;
        }
        handle_=mpv_create();
        if (!handle_) {
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
            status_=std::string("mpv option ")+key+": "+mpv_error_string(result);
            return false;
        };
        if (!required("config","no") ||
            !required("load-scripts","no") ||
            !required("ytdl","no") ||
            !required("input-default-bindings","no") ||
            !required("demuxer-lavf-o","protocol_whitelist=file") ||
            !required("vo","libmpv") ||
            !required("hwdec","auto") ||
            !required("audio-channels","stereo")) {
            close();return false;
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
                   !attach_external_aac()) {
                    status_="Required external AAC missing or unsupported by mpv";
                    close();return false;
                }
            }
        }
        const int init_result=mpv_initialize(handle_);
        if (init_result<0) {
            status_=std::string("mpv initialize: ")+mpv_error_string(init_result);
            close();return false;
        }
        mpv_render_param api[]={
            {MPV_RENDER_PARAM_API_TYPE,const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID,nullptr}
        };
        const int render_result=mpv_render_context_create(&renderer_,handle_,api);
        if (render_result<0) {
            status_=std::string("mpv renderer: ")+mpv_error_string(render_result);
            close();return false;
        }
        status_="Ready: local verified cache";
        return true;
    }
    bool attach_external_aac() {
        // No pseudo-option suffixes: use a typed array so sdmc:/ remains
        // ONE literal local path, not a colon-delimited list.
        mpv_node one{};
        one.format=MPV_FORMAT_STRING;
        one.u.string=const_cast<char*>(SwitchHlsCacheSink::kReadyExternalAac);
        mpv_node_list elements{};
        elements.num=1;
        elements.values=&one;
        mpv_node array{};
        array.format=MPV_FORMAT_NODE_ARRAY;
        array.u.list=&elements;
        const int rc=mpv_set_option(handle_,"audio-files",MPV_FORMAT_NODE,&array);
        if(rc<0) {
            status_=std::string("External audio config: ")+mpv_error_string(rc);
            return false;
        }
        return true;
    }
    bool play() {
        if (!renderer_ && !init())return false;
        const char* path=use_hls_
            ? SwitchHlsCacheSink::kReadyMaster
            : SwitchMediaCacheSink::kComplete;
        std::FILE* file=std::fopen(path,"rb");
        if (!file) {status_="Cache missing: retry video from Search";return false;}
        bool ok=std::fseek(file,0,SEEK_END)==0;
        const long bytes=ok?std::ftell(file):-1;
        std::fclose(file);
        const long max_size=use_hls_?1024L*1024:512L*1024*1024;
        const long minimum=use_hls_?20:1024;
        if (bytes<minimum || bytes>max_size) {
            status_="Completed local media missing or exceeds bounds";
            return false;
        }
        const char* cmd[]={"loadfile",path,"replace",nullptr};
        audio_gate_.reset();
        audio_failure_latched_=false;
        have_frame_=false;
        if (mpv_command(handle_,cmd)<0) {
            status_="mpv rejected completed cache";return false;
        }
        status_=use_hls_?"Opening fully cached HLS + AAC playlists...":
                          "Loading completed local MP4...";
        return true;
    }
    void pause() {
        if(!handle_)return;
        const char* cmd[]={"cycle","pause",nullptr};
        (void)mpv_command(handle_,cmd);
    }
    void seek(int sec) {
        if(!handle_)return;
        const auto arg=std::to_string(sec);
        const char* cmd[]={"seek",arg.c_str(),"relative",nullptr};
        (void)mpv_command(handle_,cmd);
    }
    void stop() {
        if(!handle_)return;
        const char* cmd[]={"stop",nullptr};
        (void)mpv_command(handle_,cmd);
        audio_gate_.reset();
        audio_failure_latched_=false;
        have_frame_=false;
        status_="Stopped. A replays completed cache.";
    }
    // The audio gate counts bounded active UI time, so sleep/wake cannot
    // instantly trip the missing-AAC timeout after a long suspend.
    // Query the selected audio stream on each active UI tick, rather than
    // assuming the first PLAYBACK_RESTART event has finalized all tracks.
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
        for(unsigned i=0;i<32;++i) {
            const mpv_event* e=mpv_wait_event(handle_,0);
            if(!e || e->event_id==MPV_EVENT_NONE)break;
            if(e->event_id==MPV_EVENT_FILE_LOADED) {
                if(use_hls_ && audio_gate_.state()==
                   playback::AudioSelectionGate::State::Idle)
                    audio_gate_.begin(playback_clock_ms());
                status_="Local media loaded; validating selected AAC audio...";
                if(!use_hls_)status_="Local MP4 loaded";
            }
            if(e->event_id==MPV_EVENT_PLAYBACK_RESTART) {
                if(use_hls_) {
                    if(audio_gate_.state()!=
                       playback::AudioSelectionGate::State::Checking)
                        audio_gate_.begin(playback_clock_ms());
                    status_="Verifying video and AAC track selection...";
                } else status_="Playing";
            }
            if(e->event_id==MPV_EVENT_END_FILE) {
                audio_gate_.reset();
                const auto* end=static_cast<const mpv_event_end_file*>(e->data);
                if(!audio_failure_latched_)
                    status_=(end && end->reason==MPV_END_FILE_REASON_ERROR)
                        ?"Decoder error opening local media":
                         "Video stopped or finished";
            }
        }
        if(use_hls_ && audio_gate_.state()==
                       playback::AudioSelectionGate::State::Checking) {
            const auto outcome=audio_gate_.observe(
                selected_audio_track(),playback_clock_ms());
            if(outcome==playback::AudioSelectionGate::State::Ready) {
                status_="Playing: HLS video with selected AAC track";
            } else if(outcome==playback::AudioSelectionGate::State::Missing) {
                audio_failure_latched_=true;
                status_="No selected AAC audio after initialization; playback stopped";
                const char* cmd[]={"stop",nullptr};
                (void)mpv_command(handle_,cmd);
                have_frame_=false;
            }
        }
    }
    const std::vector<unsigned char>& render() {
        new_frame_=false;
        if(!renderer_)return pixels_;
        const auto flags=mpv_render_context_update(renderer_);
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
                mpv_render_context_report_swap(renderer_);
            } else {
                status_=std::string("Render: ")+mpv_error_string(result);
            }
        }
        return pixels_;
    }
    bool have_frame()const{return have_frame_;}
    bool new_frame()const{return new_frame_;}
    const std::string& status()const{return status_;}
    std::string stats()const{
        if(!renderer_)return "Decoder unavailable";
        const char* h=mpv_get_property_string(handle_,"hwdec-current");
        const char* v=mpv_get_property_string(handle_,"video-codec");
        const char* a=mpv_get_property_string(handle_,"audio-codec");
        double seconds=0;
        const bool time=mpv_get_property(handle_,"time-pos",MPV_FORMAT_DOUBLE,&seconds)>=0;
        const auto str=std::string("HW ")+(h?h:"unknown")+
            " | "+(v?v:"--")+" / "+(a?a:"--")+
            " | "+(time?std::to_string(static_cast<int>(seconds)):"--")+"s"+
            " | frames "+std::to_string(frames_);
        if(h)mpv_free(const_cast<char*>(h));
        if(v)mpv_free(const_cast<char*>(v));
        if(a)mpv_free(const_cast<char*>(a));
        return str;
    }
private:
    void close() {
        if(renderer_){mpv_render_context_free(renderer_);renderer_=nullptr;}
        if(handle_){mpv_terminate_destroy(handle_);handle_=nullptr;}
        // Release ONLY after libmpv has closed every file descriptor.
        ready_lease_.release();
    }
    mpv_handle* handle_{nullptr};
    mpv_render_context* renderer_{nullptr};
    std::vector<unsigned char> pixels_;
    bool use_hls_{false};
    std::string status_{"Starting libmpv..."};
    bool have_frame_{false},new_frame_{false};
    playback::AudioSelectionGate audio_gate_{};
    playback::HlsPlaybackLease ready_lease_{};
    bool audio_failure_latched_{false};
    unsigned long long frames_{0};
};
class VideoFrame final:public brls::View {
public:
    explicit VideoFrame(CachedMpv& player):player_(player){setHeight(294);}
    void draw(NVGcontext* vg,float x,float y,float w,float h,
              brls::Style,brls::FrameContext*) override {
        const auto& pixels=player_.render();
        if(image_<0)image_=nvgCreateImageRGBA(vg,kWidth,kHeight,0,pixels.data());
        else if(player_.new_frame())nvgUpdateImage(vg,image_,pixels.data());
        nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,8);
        nvgFillColor(vg,nvgRGB(8,14,20));nvgFill(vg);
        if(image_<0||!player_.have_frame())return;
        const float scale=std::min(w/kWidth,h/kHeight);
        const float vw=kWidth*scale,vh=kHeight*scale;
        const float vx=x+(w-vw)*0.5f,vy=y+(h-vh)*0.5f;
        nvgBeginPath(vg);nvgRect(vg,vx,vy,vw,vh);
        nvgFillPaint(vg,nvgImagePattern(vg,vx,vy,vw,vh,0,image_,1));
        nvgFill(vg);
    }
private:
    CachedMpv& player_;int image_{-1};
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
    explicit M3Activity(bool hls):player_(hls),hls_(hls){active_=this;}
    ~M3Activity() override {active_=nullptr;player_.stop();}
    brls::View* createContentView() override {
        auto* root=new brls::Box(brls::Axis::COLUMN);
        root->setPadding(12,20,12,20);
        label(root,hls_?"TizenTube NX | M3 HLS Playback":
                        "TizenTube NX | M3 Normal Video",24);
        label(root,hls_?"Completed offline HLS segments + synchronized AAC":
                        "Self-contained H.264/AAC MP4 from verified SD cache",15);
        root->addView(new VideoFrame(player_));
        status_=label(root,player_.status(),17);
        info_=label(root,player_.stats(),15);
        auto* one=new brls::Box(brls::Axis::ROW);one->setHeight(56);root->addView(one);
        add_button(one,"Replay (A)",[this]{player_.play();update();});
        add_button(one,"Pause / Resume",[this]{player_.pause();update();});
        add_button(one,"Stop (X)",[this]{player_.stop();update();});
        auto* two=new brls::Box(brls::Axis::ROW);two->setHeight(56);root->addView(two);
        add_button(two,"Seek -10s (L)",[this]{player_.seek(-10);update();});
        add_button(two,"Seek +10s (R)",[this]{player_.seek(10);update();});
        root->registerAction("Back",brls::BUTTON_B,[this](brls::View*) {
            player_.stop();brls::Application::popActivity();return true;
        });
        root->registerAction("Pause",brls::BUTTON_Y,[this](brls::View*) {
            player_.pause();update();return true;
        });
        root->registerAction("Stop",brls::BUTTON_X,[this](brls::View*) {
            player_.stop();update();return true;
        });
        root->registerAction("Seek -10s",brls::BUTTON_LB,[this](brls::View*) {
            player_.seek(-10);update();return true;
        });
        root->registerAction("Seek +10s",brls::BUTTON_RB,[this](brls::View*) {
            player_.seek(10);update();return true;
        });
        player_.init();
        player_.play();
        update();
        return root;
    }
    void tick() {
        player_.pump();
        if((++ticks_%45)==0)update();
    }
    static void tick_current(){if(active_)active_->tick();}
private:
    void update(){
        if(status_)status_->setText(player_.status());
        if(info_)info_->setText(player_.stats());
    }
    static M3Activity* active_;
    CachedMpv player_;
    bool hls_{false};
    brls::Label* status_{nullptr};
    brls::Label* info_{nullptr};
    unsigned ticks_{0};
};
M3Activity* M3Activity::active_=nullptr;
} // namespace

void open_m3_completed_media(){
    brls::Application::pushActivity(new M3Activity(false));
}
void open_m3_completed_hls(){
    brls::Application::pushActivity(new M3Activity(true));
}
void tick_m3_player(){M3Activity::tick_current();}
} // namespace ttnx::switch_app
#endif // TTNX_M3_INTEGRATED
