#include "rolling_hls_worker.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace ttnx::switch_app;
using namespace std::chrono_literals;
namespace {
int failures=0;
void ck(bool p,const char* text) {if(!p){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
template<typename F> bool until(F&& f) {
    for(unsigned i=0;i<500;++i) {if(f())return true;std::this_thread::sleep_for(1ms);}
    return f();
}
struct Fake:RollingHlsWorker::Job {
    std::atomic<unsigned> pumps{0},retired{0},cleanups{0};
    std::atomic<bool> completed{false},throw_on_pump{false},block_once{false},throw_on_cleanup{false};
    std::atomic<bool> hold_retirement{false},retirement_started{false};
    std::thread::id worker_id{};
    RollingHlsWorker::Step fetch_one() override {
        worker_id=std::this_thread::get_id();
        const auto n=++pumps;
        if(throw_on_pump)throw 3;
        if(block_once)std::this_thread::sleep_for(35ms);
        if(completed)return RollingHlsWorker::Step::Complete;
        if(n==1)return RollingHlsWorker::Step::PrefixReady;
        return RollingHlsWorker::Step::Waiting;
    }
    bool retire_before(std::size_t n,std::uint64_t ms) override {
        retirement_started=true;
        while(hold_retirement)std::this_thread::sleep_for(1ms);
        if(std::this_thread::get_id()!=worker_id || n==999 || ms<4000)return false;
        ++retired;return true;
    }
    bool cleanup_after_decoder() override {
        if(throw_on_cleanup)throw 5;
        ++cleanups;return true;
    }
};
}
int main() {
    RollingHlsWorker w;Fake job;
    ck(w.start(job),"worker starts");
    ck(!w.start(job),"cannot start two producers for one media directory");
    ck(until([&]{return w.snapshot().state==RollingHlsWorker::State::Waiting;}),
       "prefix and following segment fetched asynchronously then waits");
    auto s=w.snapshot();
    ck(s.prefix_ready && s.published_segments==1 && job.pumps==2,
       "worker reports first ready prefix and does not poll HTTP while waiting");
    std::this_thread::sleep_for(25ms);
    ck(job.pumps==2,"no busy-spin or repeated CDN fetch under backpressure");
    ck(!w.request_retirement(1,9999,false) &&
       !w.request_retirement(0,9999,true),
       "retirement requires decoder release and a nonzero segment index");
    ck(w.request_retirement(999,9999,true),"worker accepts one retirement message");
    ck(!w.request_retirement(2,9999,true),"bounded control queue rejects overlapping retirement");
    ck(until([&]{return w.snapshot().rejected_retirements==1;}),
       "bad retirement is counted but not fatal");
    ck(w.snapshot().state==RollingHlsWorker::State::Waiting,
       "bad retirement does not bypass network backpressure");
    ck(w.request_retirement(1,5000,true),"valid decoder release queued");
    ck(until([&]{return job.retired==1 && job.pumps>=3;}),
       "producer resumes after confirmed media window eviction");
    job.completed=true;
    ck(w.request_retirement(2,6000,true),"resume after second release");
    ck(until([&]{return w.snapshot().worker_ended;}),"producer exits normally");
    ck(!w.cleanup_after_decoder_closed(),"must join before any cleanup");
    w.join_downloads();
    {
        ttnx::playback::HlsPlaybackLease lease;
        ck(lease.acquire() && !w.cleanup_after_decoder_closed() && job.cleanups==0,
           "active libmpv lease prevents erase even after job joined");
    }
    job.throw_on_cleanup=true;
    ck(!w.cleanup_after_decoder_closed() && job.cleanups==0,
       "unexpected SD cleanup exception preserves pending teardown without terminate");
    job.throw_on_cleanup=false;
    ck(w.cleanup_after_decoder_closed() && job.cleanups==1,
       "decoder closes then all private bytes may be deleted");
    ck(w.snapshot().state==RollingHlsWorker::State::Cleared,
       "worker can be reused after full shutdown");
    Fake failed;failed.throw_on_pump=true;
    ck(w.start(failed),"second session uses same owner");
    ck(until([&]{return w.snapshot().state==RollingHlsWorker::State::Failed;}),
       "thrown network exception fails closed");
    w.join_downloads();
    ck(w.cleanup_after_decoder_closed() && failed.cleanups==1,
       "failed worker drains and cleans without leaking media");
    Fake interrupted;interrupted.block_once=true;
    ck(w.start(interrupted),"third interrupted session starts");
    w.request_stop();
    ck(!w.cleanup_after_decoder_closed(),"stop before join cannot clear media");
    w.join_downloads();
    ck(interrupted.pumps<=1 && w.cleanup_after_decoder_closed(),
       "stop joins pending work and permits safe final teardown");
    Fake in_flight;
    RollingHlsWorker gated;
    ck(gated.start(in_flight),"handoff race worker starts");
    ck(until([&]{return gated.snapshot().state==RollingHlsWorker::State::Waiting;}),
       "handoff worker primed then backpressured");
    in_flight.hold_retirement=true;
    ck(gated.request_retirement(1,5000,true),"one decoder release queued before playback");
    ck(until([&]{return in_flight.retirement_started.load();}),
       "actual unlink callback entered on producer");
    ck(!gated.enter_player_mode(),
       "libmpv may not open while an old-media unlink is executing");
    in_flight.hold_retirement=false;
    ck(until([&]{return in_flight.retired==1;}),
       "release callback finished before reader switches modes");
    ck(until([&]{return gated.snapshot().state==RollingHlsWorker::State::Waiting;}),
       "post-retirement producer re-enters bounded wait");
    ck(gated.enter_player_mode(),
       "atomic handoff disables any future segment retirement");
    ck(!gated.request_retirement(2,9999,true),
       "active reader mode refuses timestamp-based file deletion");
    gated.request_stop();gated.join_downloads();
    ck(gated.cleanup_after_decoder_closed(),"closed reader mode safely cleans up");
    struct QuickComplete final:RollingHlsWorker::Job {
        unsigned count=0;
        RollingHlsWorker::Step fetch_one() override {
            return ++count==1?RollingHlsWorker::Step::PrefixReady:
                               RollingHlsWorker::Step::Complete;
        }
        bool retire_before(std::size_t,std::uint64_t) override {return false;}
        bool cleanup_after_decoder() override{return true;}
    } fast;
    RollingHlsWorker fast_worker;
    ck(fast_worker.start(fast),"fast fully cached session starts");
    ck(until([&]{return fast_worker.snapshot().state==RollingHlsWorker::State::Complete;}),
       "fast session finishes before the UI opens player");
    ck(fast_worker.enter_player_mode(),
       "verified finished local HLS remains openable without redownloading");
    fast_worker.join_downloads();
    ck(fast_worker.cleanup_after_decoder_closed(),"finished stream cleanup passes");
    if(failures)std::cerr<<failures<<" M4 worker test failures\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
