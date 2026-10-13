#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
using namespace ttnx::playback;
namespace fs=std::filesystem;

struct FakeVodReader {
    std::string ts;
    std::atomic<std::size_t> requests{0};
    struct Reply {bool ok{};std::size_t bytes{};};
    template<class Admit,class Emit>
    Reply stream(std::string_view,Admit admit,Emit emit) {
        ++requests;
        if(!admit(ts.size()))return {};
        for(std::size_t p=0;p<ts.size();p+=32768)
            if(!emit(std::string_view(ts).substr(p,32768)))return {};
        return {true,ts.size()};
    }
};
static std::string sample_ts() {
    // Same in-band H264/AAC PAT+PMT fixture as the native epoch tests.
    std::string b(300*188,char(0xff));
    for(std::size_t i=0;i<300;++i)b[i*188]=char(0x47);
    auto put=[&](std::size_t at,std::initializer_list<unsigned char> v) {
        for(auto c:v)b[at++]=char(c);
    };
    put(0,{0x47,0x40,0x00,0x10,0,0x00,0xb0,0x0d,0,1,0xc1,
           0,0,0,1,0xe1,0,0,0,0,0});
    put(188,{0x47,0x41,0x00,0x10,0,0x02,0xb0,23,
             0,1,0xc1,0,0,0xe1,1,0xf0,0,0x1b,0xe1,1,0xf0,0});
    put(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return b;
}
int main() {
    const auto dir=fs::temp_directory_path()/"ttnx-m4-long-vod";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto source=std::make_shared<FakeVodReader>();
    source->ts=sample_ts();
    // 210 x 3 seconds = 10 min 30 sec. Only TWO verified TS segments
    // ever coexist on SD. No whole-video file or growing RAM buffer.
    constexpr std::size_t segments=210;
    std::vector<std::string> urls;
    for(std::size_t i=0;i<segments;++i)
        urls.push_back("seg"+std::to_string(i));
    const auto path=(dir/"stream.part").string();
    const auto cap=2*source->ts.size();
    auto epoch=RollingStreamEpoch::with_reader(
        path,std::move(urls),source,
        [](std::string_view url){return url.substr(0,3)=="seg";},
        cap,std::vector<std::uint64_t>(segments,3000));
    assert(epoch);
    for(std::size_t start=0;start<segments;start+=2) {
        if(start) {
            assert(epoch->restart_at(start));
            assert(epoch->snapshot().epoch_origin_segment==start);
        }
        assert(epoch->start());
        bool done=false;
        for(int tick=0;tick<5000;++tick) {
            const auto phase=epoch->snapshot().phase;
            if(phase==RollingStreamEpoch::Phase::Backpressure ||
               phase==RollingStreamEpoch::Phase::Complete) {
                done=true;break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        assert(done);
        const auto snap=epoch->snapshot();
        assert(snap.next_segment_index==start+2);
        assert(snap.segments_committed==2);
        assert(snap.peak_file_bytes<=cap);
        assert(snap.published_bytes==cap);
        assert(fs::exists(path) && fs::file_size(path)<=cap);
        const auto expected=(start+2<segments)
            ?RollingStreamEpoch::Phase::Backpressure
            :RollingStreamEpoch::Phase::Complete;
        assert(snap.phase==expected);
        mpv_handle handle{};
        assert(epoch->register_decoder(&handle));
        mpv_stream_cb_info callback{};
        char uri[]="ttnxring://media";
        assert(g_open_fn(g_user_data,uri,&callback)==0);
        assert(callback.cookie && callback.read_fn);
        std::string decoded(cap,'\0');
        std::size_t received=0;
        while(received<cap) {
            const auto n=callback.read_fn(callback.cookie,
                decoded.data()+received,cap-received);
            assert(n>0 && static_cast<std::size_t>(n)<=cap-received);
            received+=static_cast<std::size_t>(n);
        }
        assert(decoded==source->ts+source->ts);
        char extra{};
        assert(callback.read_fn(callback.cookie,&extra,1)==0);
        epoch->stop_and_join();
        assert(!epoch->notify_decoder_destroyed()); // live mpv lease
        callback.close_fn(callback.cookie);
        assert(epoch->notify_decoder_destroyed());
        assert(epoch->cleanup());
        assert(!fs::exists(path)); // no retained watched video after epoch
    }
    assert(source->requests.load()>=segments);
    assert(source->requests.load()<2*segments);
    assert(!epoch->restart_at(segments));
    epoch.reset();
    fs::remove_all(dir);
}
