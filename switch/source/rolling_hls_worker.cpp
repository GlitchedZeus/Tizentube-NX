#if defined(TTNX_M4_ROLLING_HOST_TEST) || defined(TTNX_M4_ROLLING_STREAM)
#include "rolling_hls_worker.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <utility>

namespace ttnx::switch_app {
RollingHlsWorker::~RollingHlsWorker() {
    request_stop();
    join_downloads();
    // Deliberately do not delete SD files in the destructor: libmpv might
    // still own an FD. Cleanup requires explicit decoder-close evidence.
}

bool RollingHlsWorker::start(Job& job) {
    std::lock_guard lock(mutex_);
    if(worker_.joinable() || job_ ||
       (status_.state!=State::Idle && status_.state!=State::Cleared))
        return false;
    job_=&job;
    stop_=false;
    pending_.reset();
    status_={};
    status_.state=State::Fetching;
    try {
        worker_=std::thread([this]{run();});
    } catch(...) {
        job_=nullptr;
        status_={};
        return false;
    }
    return true;
}

RollingHlsWorker::Snapshot RollingHlsWorker::snapshot() const noexcept {
    std::lock_guard lock(mutex_);
    return status_;
}

bool RollingHlsWorker::request_retirement(
    std::size_t first_index,std::uint64_t playhead_ms,bool released) {
    if(!released || first_index==0)return false;
    std::lock_guard lock(mutex_);
    if(!job_ || stop_ || pending_ || status_.worker_ended ||
       (status_.state!=State::Fetching &&
        status_.state!=State::PrefixReady &&
        status_.state!=State::Waiting))return false;
    pending_=Retirement{first_index,playhead_ms};
    wake_.notify_one();
    return true;
}

void RollingHlsWorker::request_stop() noexcept {
    std::lock_guard lock(mutex_);
    stop_=true;
    pending_.reset();
    if(job_ && !status_.worker_ended)
        status_.state=State::StopRequested;
    wake_.notify_one();
}

void RollingHlsWorker::join_downloads() noexcept {
    // Caller must not race this with start() or call it from worker thread.
    if(worker_.joinable())worker_.join();
    std::lock_guard lock(mutex_);
    if(job_ && status_.worker_ended)status_.state=State::Joined;
}

bool RollingHlsWorker::cleanup_after_decoder_closed() noexcept {
    std::lock_guard lock(mutex_);
    if(!job_ || worker_.joinable() || !status_.worker_ended ||
       playback::HlsPlaybackLease::is_active())return false;
    // An injected/native cleanup failure must never terminate the app.
    // Leave job ownership and cache accounting intact so cleanup can retry.
    bool cleaned=false;
    try {cleaned=job_->cleanup_after_decoder();}catch(...){return false;}
    if(!cleaned)return false;
    job_=nullptr;
    pending_.reset();
    stop_=false;
    status_.state=State::Cleared;
    return true;
}

void RollingHlsWorker::run() noexcept {
    for(;;) {
        Job* job=nullptr;
        std::optional<Retirement> retirement;
        {
            std::unique_lock lock(mutex_);
            if(stop_)break;
            if(status_.state==State::Waiting) {
                wake_.wait(lock,[&]{return stop_||pending_.has_value();});
                if(stop_)break;
            }
            if(pending_) {
                retirement=std::exchange(pending_,std::nullopt);
            }
            job=job_;
        }
        if(!job)break;
        if(retirement) {
            bool ok=false;
            try {ok=job->retire_before(retirement->first_index,
                                        retirement->playhead_ms);}catch(...){ok=false;}
            std::lock_guard lock(mutex_);
            if(!ok)++status_.rejected_retirements;
            else if(status_.state==State::Waiting)status_.state=State::Fetching;
            // A false/early release never credits bytes or spins the worker.
            continue;
        }
        Step outcome=Step::Failed;
        try {outcome=job->fetch_one();}catch(...){outcome=Step::Failed;}
        std::lock_guard lock(mutex_);
        if(stop_)break;
        switch(outcome) {
        case Step::Fetching:
            status_.state=status_.prefix_ready?State::PrefixReady:State::Fetching;
            ++status_.published_segments;
            break;
        case Step::PrefixReady:
            status_.state=State::PrefixReady;
            status_.prefix_ready=true;
            ++status_.published_segments;
            break;
        case Step::Waiting:
            status_.state=State::Waiting;
            break;
        case Step::Complete:
            status_.state=State::Complete;
            status_.worker_ended=true;
            return;
        case Step::Failed:
            status_.state=State::Failed;
            status_.worker_ended=true;
            return;
        }
    }
    std::lock_guard lock(mutex_);
    status_.worker_ended=true;
    status_.state=State::StopRequested;
}
} // namespace ttnx::switch_app

#endif // opt-in rolling HLS worker
