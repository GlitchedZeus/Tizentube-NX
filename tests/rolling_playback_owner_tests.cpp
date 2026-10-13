#include "rolling_playback_owner.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
using ttnx::switch_app::RollingPlaybackOwner;
using ttnx::switch_app::RollingHlsWorker;
using ttnx::playback::HlsPlaybackLease;
using namespace std::chrono_literals;
namespace {
int failures=0;
void ck(bool value,const char* what){if(!value){++failures;std::cerr<<"FAIL "<<what<<'\n';}}
template<class F> bool until(F&& p){for(int i=0;i<500;++i){if(p())return true;std::this_thread::sleep_for(2ms);}return p();}
struct Counters {std::atomic<int> fetched{0},removed{0},destroyed{0};
    std::atomic<bool> block{false},fail_cleanup{false},fail_fetch{false};};
struct Job final : RollingHlsWorker::Job {
    using Step=RollingHlsWorker::Step;
    explicit Job(std::shared_ptr<Counters> counters):c(std::move(counters)){}
    ~Job() override {++c->destroyed;}
    Step fetch_one() override {
        ++c->fetched;
        if(c->block.load())std::this_thread::sleep_for(20ms);
        if(c->fail_fetch.load())return Step::Failed;
        if(c->fetched==1)return Step::PrefixReady;
        return Step::Waiting;
    }
    bool retire_before(std::size_t,std::uint64_t) override {
        return false; // live decoder never confirms FD release
    }
    bool cleanup_after_decoder() override {
        if(c->fail_cleanup)throw 7;
        ++c->removed;return true;
    }
    std::shared_ptr<Counters> c;
};
}
int main() {
    RollingPlaybackOwner session;
    auto counts=std::make_shared<Counters>();
    ck(session.start(std::make_unique<Job>(counts)),"own single network job");
    ck(!session.begin_decoder(),"no decoder before verified prefix");
    auto rejected=std::make_shared<Counters>();
    const bool rejected_start=session.start(std::make_unique<Job>(rejected));
    ck(!rejected_start && rejected->destroyed==1,"cannot replace active job");
    ck(until([&]{return session.snapshot().state==RollingHlsWorker::State::Waiting;}),
       "background worker publishes verified prefix then blocks");
    ck(session.begin_decoder() && session.owns_decoder_lease() &&
       HlsPlaybackLease::is_active(),"exclusive decoder lease obtained only after prefix");
    ck(!session.begin_decoder(),"double-opening libmpv blocked");
    ck(!session.cleanup() && counts->removed==0 &&
       !session.notify_decoder_destroyed(),"cannot erase or release before stop-and-join");
    session.request_stop();
    session.join_downloads();
    ck(!session.cleanup() && HlsPlaybackLease::is_active(),
       "no deletion after worker join but before libmpv destroyed");
    ck(session.notify_decoder_destroyed() &&
       !HlsPlaybackLease::is_active(),
       "only explicit post-mpv destroy acknowledgement releases lease");
    ck(!session.notify_decoder_destroyed(),"duplicate decoder close rejected");
    counts->fail_cleanup=true;
    ck(!session.cleanup() && counts->removed==0 && counts->destroyed==0 &&
       session.phase()==RollingPlaybackOwner::Phase::FailedCleanup,
       "cleanup exception retains job and media for retry");
    counts->fail_cleanup=false;
    ck(session.cleanup() && counts->removed==1 && counts->destroyed==1 &&
       session.phase()==RollingPlaybackOwner::Phase::Idle,
       "successful retry clears media and destroys job safely");
    auto c2=std::make_shared<Counters>();
    c2->fail_fetch=true;
    ck(session.start(std::make_unique<Job>(c2)),"owner reuses cleared session");
    ck(until([&]{return session.snapshot().state==RollingHlsWorker::State::Failed;}),
       "failed network worker captured");
    ck(!session.begin_decoder(),"no decoder after worker failure");
    session.join_downloads();
    ck(session.cleanup() && c2->removed==1 && c2->destroyed==1,
       "failed fetch never escapes zero-cache cleanup requirement");
    {
        HlsPlaybackLease foreign;
        ck(foreign.acquire(),"foreign player lease protects SD");
        auto c3=std::make_shared<Counters>();
        const bool alien_start=session.start(std::make_unique<Job>(c3));
        ck(!alien_start && c3->destroyed==1,"cannot start downloader over another active playback lease");
    }
    auto c4=std::make_shared<Counters>();
    c4->block=true;
    {
        RollingPlaybackOwner transient;
        ck(transient.start(std::make_unique<Job>(c4)),"transient owner starts");
        ck(until([&]{return c4->fetched>0;}),"inflight HTTPS exists");
        // Destructor must stop and join before destroying the Job; media
        // cannot be automatically erased while a decoder might be active.
    }
    ck(c4->destroyed==1 && c4->removed==0,
       "destructor joins running producer, preserves media pending explicit teardown");
    if(failures)std::cerr<<failures<<" ownership contract failures\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
