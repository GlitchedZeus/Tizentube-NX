#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
namespace fs=std::filesystem;
using namespace ttnx::playback;
struct FakeReader {
    std::string bytes;
    std::atomic<int> calls{0};
    struct Result {bool ok{};std::size_t bytes{};};
    template<class Admit,class Emit>
    Result stream(std::string_view,Admit admit,Emit emit) {
        ++calls;
        if(!admit(bytes.size()))return {};
        for(std::size_t off=0;off<bytes.size();off+=32768) {
            if(!emit(std::string_view(bytes).substr(off,
                     std::min<std::size_t>(32768,bytes.size()-off))))return {};
        }
        return {true,bytes.size()};
    }
};
std::string make_ts(std::size_t packets) {
    std::string b(packets*188,char(0xff));
    for(std::size_t i=0;i<packets;++i)b[i*188]=0x47;
    const auto put=[&](std::size_t at,std::initializer_list<unsigned char> v){
        for(auto c:v)b[at++]=static_cast<char>(c);};
    put(0,{0x47,0x40,0x00,0x10,0,0x00,0xb0,0x0d,0,1,0xc1,
           0,0,0,1,0xe1,0,0,0,0,0});
    put(188,{0x47,0x41,0x00,0x10,0,0x02,0xb0,23,
             0,1,0xc1,0,0,0xe1,1,0xf0,0,0x1b,0xe1,1,0xf0,0});
    put(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return b;
}
int main(){
    const fs::path dir=fs::temp_directory_path()/"ttnx-owned-epoch-test";
    fs::remove_all(dir);fs::create_directory(dir);
    auto reader=std::make_shared<FakeReader>();
    reader->bytes=make_ts(300); // 56,400 bytes each, cap 112,800 bytes
    const std::string path=(dir/"stream.part").string();
    auto epoch=RollingStreamEpoch::with_reader(path,
         {"segment0","segment1","segment2","segment3","segment4"},
         reader,[](std::string_view url){return url.substr(0,7)=="segment";},112800);
    assert(epoch);
    auto rejected=RollingStreamEpoch::with_reader((dir/"bad.part").string(),
        {"https://unapproved.com/path"},reader,
        [](std::string_view u){return u.substr(0,7)=="segment";},112800);
    assert(!rejected && !fs::exists(dir/"bad.part"));
    assert(epoch->start());
    for(int i=0;i<5000 && epoch->snapshot().phase!=RollingStreamEpoch::Phase::Backpressure;++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    auto snap=epoch->snapshot();
    assert(snap.phase==RollingStreamEpoch::Phase::Backpressure);
    assert(snap.segments_committed==2 && snap.published_bytes==112800);
    mpv_handle handle{};
    assert(epoch->register_decoder(&handle));
    mpv_stream_cb_info callback{};
    char url[]="ttnxring://media";
    assert(g_open_fn(g_user_data,url,&callback)==0);
    assert(callback.cookie);
    assert(!epoch->cleanup());
    epoch->stop_and_join();
    assert(!epoch->notify_decoder_destroyed()); // callback still open
    callback.close_fn(callback.cookie);
    assert(epoch->notify_decoder_destroyed());
    assert(epoch->cleanup());
    assert(!fs::exists(path));
    assert(epoch->restart_at(2));
    assert(epoch->start());
    for(int i=0;i<5000 && epoch->snapshot().phase!=RollingStreamEpoch::Phase::Backpressure;++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    assert(epoch->snapshot().phase==RollingStreamEpoch::Phase::Backpressure);
    assert(epoch->snapshot().segments_committed==2);
    epoch->stop_and_join();
    assert(epoch->cleanup());
    assert(!fs::exists(path));
    assert(epoch->restart_at(4));
    assert(epoch->start());
    for(int i=0;i<5000 && epoch->snapshot().phase!=RollingStreamEpoch::Phase::Complete;++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    assert(epoch->snapshot().phase==RollingStreamEpoch::Phase::Complete);
    epoch->stop_and_join();
    assert(epoch->cleanup());
    assert(!fs::exists(path));
    assert(!epoch->restart_at(6));
    epoch.reset();reader.reset();
    fs::remove_all(dir);
}
