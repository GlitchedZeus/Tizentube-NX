#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>
using namespace ttnx::playback;
namespace fs=std::filesystem;
struct FakeMedia {
    std::string ts;
    std::atomic<unsigned> fetched{0};
    struct Reply {bool ok{false};std::size_t bytes{0};};
    template<class Admit,class Emit>
    Reply stream(std::string_view,Admit admit,Emit emit) {
        ++fetched;
        if(!admit(ts.size()))return {};
        for(std::size_t pos=0;pos<ts.size();pos+=4096)
            if(!emit(std::string_view(ts).substr(pos,4096)))return {};
        return {true,ts.size()};
    }
};
static std::string media() {
    std::string s(188*128,char(0xff));
    for(std::size_t i=0;i<128;++i)s[i*188]=char(0x47);
    auto fill=[&](std::size_t off,std::initializer_list<unsigned char> bytes) {
        for(auto x:bytes)s[off++]=char(x);
    };
    fill(0,{0x47,0x40,0,0x10,0,0,0xb0,0x0d,0,1,0xc1,0,0,0,1,0xe1,0,0,0,0,0});
    fill(188,{0x47,0x41,0,0x10,0,2,0xb0,23,0,1,0xc1,0,0,0xe1,1,0xf0,0,0x1b,0xe1,1,0xf0,0});
    fill(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return s;
}
static bool wait_for(const std::function<bool()>& done) {
    for(int i=0;i<2000;++i) {
        if(done())return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
int main(){
    const auto dir=fs::temp_directory_path()/"ttnx_prefetch_epoch";
    fs::remove_all(dir);fs::create_directory(dir);
    auto source=std::make_shared<FakeMedia>();source->ts=media();
    std::vector<std::string> urls;
    for(int i=0;i<12;++i)urls.push_back("segment_"+std::to_string(i));
    auto epoch=RollingStreamEpoch::with_reader((dir/"media.part").string(),
        urls,source,[](std::string_view v){return v.substr(0,8)=="segment_";},
        12*source->ts.size(),std::vector<std::uint64_t>(12,3000));
    assert(epoch && epoch->start());
    using Phase=RollingStreamEpoch::Phase;
    assert(wait_for([&] {return epoch->snapshot().phase==Phase::WaitingForPlayback;}));
    assert(source->fetched.load()==6);
    auto before=epoch->snapshot();
    assert(before.waiting_for_playback && before.segments_committed==6);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(source->fetched.load()==6);
    mpv_handle handle{};
    assert(epoch->register_decoder(&handle));
    mpv_stream_cb_info cookie{};
    char protocol[]="ttnxring://media";
    assert(g_open_fn(g_user_data,protocol,&cookie)==0);
    epoch->advance_playback_ms(2999);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(source->fetched.load()==6);
    epoch->advance_playback_ms(3000);
    assert(wait_for([&] {return epoch->snapshot().segments_committed==7;}));
    assert(source->fetched.load()==7);
    epoch->advance_playback_ms(18000);
    assert(wait_for([&] {return epoch->snapshot().phase==Phase::Complete;}));
    assert(epoch->snapshot().segments_committed==12);
    epoch->stop_and_join();
    assert(!epoch->notify_decoder_destroyed()); // live libmpv reader
    cookie.close_fn(cookie.cookie);
    assert(epoch->notify_decoder_destroyed());
    assert(epoch->cleanup()&&!fs::exists(dir/"media.part"));
    assert(epoch->restart_at(8));
    assert(epoch->start());
    assert(wait_for([&] {return epoch->snapshot().phase==Phase::Complete;}));
    assert(epoch->snapshot().segments_committed==4); // relative epoch
    epoch->stop_and_join();
    assert(epoch->cleanup()&&!fs::exists(dir/"media.part"));
    assert(epoch->restart_at(0)&&epoch->start());
    assert(wait_for([&] {return epoch->snapshot().phase==Phase::WaitingForPlayback;}));
    const auto initial=source->fetched.load();
    const auto start=std::chrono::steady_clock::now();
    epoch->stop_and_join(); // must wake waiting worker immediately
    assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
    assert(source->fetched.load()==initial);
    assert(epoch->cleanup()&&!fs::exists(dir/"media.part"));
    fs::remove_all(dir);
}
