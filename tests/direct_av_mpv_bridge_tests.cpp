#include "tizentube_nx/playback/direct_av_mpv_bridge.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
using namespace ttnx;
class Fake final:public playback::MediaChunkTransport {
public:
    std::string file;
    std::atomic<bool> block_once{false},entered{false},release{false};
    net::HttpResult perform(const net::HttpRequest& req)override {
        const auto value=req.headers[0].value;
        const auto dash=value.find('-');
        const auto first=std::stoull(value.substr(6,dash-6));
        if(block_once.exchange(false,std::memory_order_acq_rel)) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto last=std::min<std::uint64_t>(
            std::stoull(value.substr(dash+1)),file.size()-1);
        net::HttpResult r;r.ok=true;r.response.status_code=206;
        r.response.body=file.substr(first,last-first+1);
        r.response.headers={{"Content-Type","video/mp4"},
            {"Content-Length",std::to_string(r.response.body.size())},
            {"Content-Range","bytes "+std::to_string(first)+"-"+
                std::to_string(last)+"/"+std::to_string(file.size())}};
        return r;
    }
};
int main(){
    const std::string url="https://r1---sn-abcd.googlevideo.com/videoplayback?x=1";
    Fake video,audio;
    video.file=std::string(650000,'v');audio.file=std::string(500000,'a');
    const std::string prefix=std::string("\0\0\0\x18""ftypmp42",12);
    video.file.replace(0,12,prefix);
    audio.file.replace(0,12,prefix);
    playback::MediaChunkReader vr(video),ar(audio);
    auto v=std::make_shared<playback::BoundedDirectTrack>(vr,url);
    auto a=std::make_shared<playback::BoundedDirectTrack>(ar,url);
    assert(v->prime()&&a->prime());
    playback::DirectAvMpvBridge bridge(v,a);
    const auto uri=playback::DirectAvMpvBridge::edl_uri();
    assert(uri.find("edl://%")==0);
    assert(uri.find(";!new_stream;%")!=std::string::npos);
    assert(uri.find("https:")==std::string::npos);
    mpv_stream_cb_info vinfo{},ainfo{},invalid{};
    char bad[]="ttnxav://arbitrary";
    assert(playback::DirectAvMpvBridge::open(&bridge,bad,&invalid)<0);
    char videouri[]="ttnxav://video",audiouri[]="ttnxav://audio";
    assert(playback::DirectAvMpvBridge::open(&bridge,videouri,&vinfo)==0);
    assert(playback::DirectAvMpvBridge::open(&bridge,audiouri,&ainfo)==0);
    assert(vinfo.cookie!=ainfo.cookie);
    assert(vinfo.size_fn(vinfo.cookie)==650000);
    assert(ainfo.size_fn(ainfo.cookie)==500000);
    char part[1024]{};
    assert(vinfo.read_fn(vinfo.cookie,part,32)==32);
    assert(std::memcmp(part,video.file.data(),32)==0);
    assert(ainfo.read_fn(ainfo.cookie,part,32)==32);
    assert(std::memcmp(part,audio.file.data(),32)==0);
    assert(vinfo.seek_fn(vinfo.cookie,600000)==600000);
    assert(vinfo.read_fn(vinfo.cookie,part,32)==32);
    assert(std::memcmp(part,video.file.data()+600000,32)==0);
    assert(ainfo.seek_fn(ainfo.cookie,499999)==499999);
    assert(ainfo.read_fn(ainfo.cookie,part,32)==1);
    assert(ainfo.read_fn(ainfo.cookie,part,32)==0);
    assert(vinfo.seek_fn(vinfo.cookie,-1)<0);
    assert(vinfo.seek_fn(vinfo.cookie,650001)<0);
    ainfo.cancel_fn(ainfo.cookie);
    assert(ainfo.read_fn(ainfo.cookie,part,32)<0);
    // mpv can reopen the SAME audio source after a seek-time cancel.
    mpv_stream_cb_info audio_reopened{};
    assert(playback::DirectAvMpvBridge::open(&bridge,audiouri,
                                            &audio_reopened)==0);
    assert(audio_reopened.seek_fn(audio_reopened.cookie,150000)==150000);
    assert(audio_reopened.read_fn(audio_reopened.cookie,part,32)==32);
    assert(std::memcmp(part,audio.file.data()+150000,32)==0);
    audio_reopened.close_fn(audio_reopened.cookie);
    // Race a slow HTTPS media read against an exact later seek. The reader
    // must reject obsolete returned bytes and not advance the new cursor.
    mpv_stream_cb_info raced{};
    assert(playback::DirectAvMpvBridge::open(&bridge,videouri,&raced)==0);
    assert(raced.seek_fn(raced.cookie,262144)==262144);
    video.block_once.store(true);
    std::atomic<std::int64_t> obsolete{0};
    std::thread pending([&]{
        char old_data[1024]{};
        obsolete.store(raced.read_fn(raced.cookie,old_data,sizeof(old_data)),
                       std::memory_order_release);
    });
    for(int i=0;i<5000 && !video.entered.load();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(video.entered.load());
    // A blocked HTTPS call cannot freeze the UI statistics or seek callback.
    const auto started=std::chrono::steady_clock::now();
    assert(v->size()==650000);
    assert(v->retained_bytes()<=2*playback::BoundedDirectTrack::kWindow);
    assert(v->fetched_ranges()>0);
    assert(v->range_retries()==0);
    assert(raced.seek_fn(raced.cookie,327680)==327680);
    assert(std::chrono::steady_clock::now()-started<
           std::chrono::milliseconds(500));
    video.release.store(true);
    pending.join();
    assert(obsolete.load()<0);
    char fresh[1024]{};
    assert(raced.read_fn(raced.cookie,fresh,sizeof(fresh))==1024);
    assert(std::memcmp(fresh,video.file.data()+327680,sizeof(fresh))==0);
    raced.close_fn(raced.cookie);
    // A per-cookie cancellation does not poison independent video transport.
    assert(vinfo.seek_fn(vinfo.cookie,0)==0);
    assert(vinfo.read_fn(vinfo.cookie,part,8)==8);
    vinfo.close_fn(vinfo.cookie);
    ainfo.close_fn(ainfo.cookie);
    bridge.cancel();
    assert(v->read_at(0,part,12)<0);
}
