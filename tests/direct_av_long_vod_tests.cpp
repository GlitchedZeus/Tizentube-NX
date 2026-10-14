#include "tizentube_nx/playback/bounded_direct_track.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
using namespace ttnx;
class Virtual35MinuteMedia final:public playback::MediaChunkTransport {
public:
    explicit Virtual35MinuteMedia(std::uint64_t length):total(length){}
    static char at(std::uint64_t offset){
        constexpr char header[12]={0,0,0,24,'f','t','y','p','m','p','4','2'};
        if(offset<12)return header[offset];
        return static_cast<char>('A'+(offset%25));
    }
    net::HttpResult perform(const net::HttpRequest& req) override {
        const auto raw=req.headers[0].value;
        const auto dash=raw.find('-');
        assert(raw.starts_with("bytes=")&&dash!=std::string::npos);
        const auto first=std::stoull(raw.substr(6,dash-6));
        const auto asked=std::stoull(raw.substr(dash+1));
        assert(asked>=first&&first<total);
        const auto last=std::min<std::uint64_t>(asked,total-1);
        assert(last-first+1<=playback::BoundedDirectTrack::kWindow);
        net::HttpResult out;out.ok=true;out.response.status_code=206;
        out.response.body.resize(last-first+1);
        for(std::uint64_t i=0;i<out.response.body.size();++i)
            out.response.body[i]=at(first+i);
        out.response.headers={{"Content-Type","video/mp4"},
            {"Content-Length",std::to_string(out.response.body.size())},
            {"Content-Range","bytes "+std::to_string(first)+"-"+
                std::to_string(last)+"/"+std::to_string(total)}};
        calls.fetch_add(1,std::memory_order_relaxed);
        return out;
    }
    std::uint64_t total;
    std::atomic<unsigned> calls{0};
};
int main() {
    // 35 min, 224 MB video + 26 MB AAC, NOT copied onto SD and never held
    // in application memory. Each fake HTTPS 206 materializes <=256 KiB.
    Virtual35MinuteMedia video(224ULL*1024*1024),
                         audio(26ULL*1024*1024);
    playback::MediaChunkReader vr(video),ar(audio);
    const std::string url=
        "https://r1---sn-test.googlevideo.com/videoplayback?foo=123";
    playback::BoundedDirectTrack v(vr,url),a(ar,url);
    assert(v.prime()&&a.prime());
    assert(v.size()==static_cast<std::int64_t>(video.total));
    assert(a.size()==static_cast<std::int64_t>(audio.total));
    auto worker=[](playback::BoundedDirectTrack& track,
                   const Virtual35MinuteMedia& media) {
        char buffer[32768]{};
        // Repeated jumps include the last few seconds of long VOD and
        // reverse seeking. Neither stream's 256KiB memory bound changes.
        for(unsigned i=0;i<128;++i) {
            const auto pos=(static_cast<std::uint64_t>(i)*7266479ULL)%
                (media.total-32768);
            const auto received=track.read_at(pos,buffer,sizeof(buffer));
            assert(received>0 && received<=32768);
            assert(buffer[0]==Virtual35MinuteMedia::at(pos));
            assert(track.retained_bytes()<=2*playback::BoundedDirectTrack::kWindow);
        }
        assert(track.fetched_ranges()>=50);
    };
    std::thread one([&]{worker(v,video);});
    std::thread two([&]{worker(a,audio);});
    one.join();two.join();
    assert(v.retained_bytes()+a.retained_bytes()<=4*playback::BoundedDirectTrack::kWindow);
    assert(video.calls.load()>50&&audio.calls.load()>50);
    v.cancel();a.cancel();
    char out[32]{};
    assert(v.read_at(0,out,sizeof(out))<0);
    assert(a.read_at(0,out,sizeof(out))<0);
}
