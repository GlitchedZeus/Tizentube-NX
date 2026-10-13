// Opt-in native transport cancellation contract, with a real owned epoch.
// Simulate an HTTPS segment held between admission and its next 32KiB chunk.
#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using ttnx::playback::RollingStreamEpoch;
struct InterruptibleReader {
    std::atomic<bool> cancelled{false},entered{false};
    std::atomic<int> cancels{0},resets{0},requests{0};
    struct Reply{bool ok{false};std::size_t bytes{0};};
    void cancel()noexcept{cancelled.store(true);++cancels;}
    void reset_cancel()noexcept{cancelled.store(false);++resets;}
    template<class Admit,class Emit>
    Reply stream(std::string_view,Admit admit,Emit) {
        ++requests;
        entered.store(true);
        // Let the producer receive Stop while the network is in-flight.
        for(int i=0;i<2000 && !cancelled.load();++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(cancelled.load())return {};
        // Never admit or publish an invalid/unreviewed TS body.
        return {false,0};
    }
};
int main() {
    const auto root=fs::temp_directory_path()/"ttnx-m4-native-cancel";
    fs::remove_all(root);fs::create_directory(root);
    auto reader=std::make_shared<InterruptibleReader>();
    const auto path=(root/"media.part").string();
    auto epoch=RollingStreamEpoch::with_reader(path,{"segment-0"},reader,
        [](std::string_view u){return u=="segment-0";},65536,{3000});
    assert(epoch && epoch->start());
    for(int i=0;i<5000 && !reader->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    assert(reader->entered.load());
    const auto start=std::chrono::steady_clock::now();
    epoch->request_stop();
    epoch->stop_and_join();
    assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
    assert(reader->cancelled.load() && reader->cancels.load()==1);
    assert(reader->requests.load()==1 && reader->resets.load()==1);
    assert(epoch->snapshot().segments_committed==0);
    assert(epoch->cleanup());
    assert(!fs::exists(path));
    assert(epoch->restart_at(0));
    reader->entered.store(false);
    assert(epoch->start()); // cancel bit reset ONLY after safe unlink/restart
    for(int i=0;i<5000 && !reader->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    assert(reader->entered.load() && !reader->cancelled.load());
    epoch->stop_and_join();
    assert(reader->cancels.load()==2 && reader->resets.load()==2);
    assert(reader->requests.load()==2);
    assert(epoch->cleanup() && !fs::exists(path));
    fs::remove_all(root);
}
