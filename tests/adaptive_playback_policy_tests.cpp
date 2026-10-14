#include "tizentube_nx/playback/adaptive_media_budget.hpp"
#include "tizentube_nx/playback/local_adaptive_edl.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
int main(){
    using namespace ttnx::playback;
    const auto av=local_adaptive_edl(
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a");
    assert(av&&av->starts_with("edl://%"));
    assert(av->find(";!new_stream;%")!=std::string::npos);
    assert(!local_adaptive_edl("https://evil/adaptive_video.mp4",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a"));
    assert(!local_adaptive_edl("sdmc:/switch/TizenTube-NX/media-cache/active.mp4",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a"));
    assert(!local_adaptive_edl("sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4",
        "sdmc:/switch/TizenTube-NX/media-cache/../adaptive_audio.m4a"));
    AdaptiveMediaBudget short_video;
    assert(short_video.admit(45000000)&&short_video.admit(7000000));
    assert(short_video.reserved()==52000000);
    assert(!short_video.admit(10000000));
    AdaptiveMediaBudget huge;
    assert(!huge.admit(61461733)&&!huge.admit(0));
    assert(huge.admit(38000000)&&!huge.admit(61000000));
    std::atomic<int> accepted{0};
    AdaptiveMediaBudget simultaneous;
    std::vector<std::thread> threads;
    for(int i=0;i<12;++i) threads.emplace_back([&]{
        if(simultaneous.admit(10000000))++accepted;
    });
    for(auto& t:threads)t.join();
    assert(accepted==6);
    assert(simultaneous.reserved()==AdaptiveMediaBudget::kMaxCompletedPair);
}
