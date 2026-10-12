#pragma once
#if defined(TTNX_M4_ROLLING_STREAM) || defined(TTNX_M4_ROLLING_HOST_TEST)
#include "rolling_hls_feed.hpp"
#include "rolling_hls_worker.hpp"

namespace ttnx::switch_app {
// Exact native adapter: no second downloader or bypass around the real
// approved HlsSegmentReader, codec gates, and fsynced SD transactions.
// begin(plan) must succeed on the owning thread BEFORE worker.start(job).
class RollingMuxedHlsWorkerJob final : public RollingHlsWorker::Job {
public:
    using Step=RollingHlsWorker::Step;
    explicit RollingMuxedHlsWorkerJob(RollingMuxedHlsSession& session)
        : session_(session) {}
    Step fetch_one() override {
        using State=RollingMuxedHlsSession::State;
        switch(session_.pump()) {
        case State::Fetching: return Step::Fetching;
        case State::PrefixReady: return Step::PrefixReady;
        case State::Waiting: return Step::Waiting;
        case State::Complete: return Step::Complete;
        default: return Step::Failed;
        }
    }
    bool retire_before(std::size_t first_index,
                       std::uint64_t playhead_ms) override {
        // The worker only receives retirement messages from a caller that
        // positively acknowledged decoder closure for those specific FDs.
        // Native proof of per-segment FD retirement is NOT implemented yet.
        return session_.retire_before(first_index,playhead_ms,true);
    }
    bool cleanup_after_decoder() override {
        return session_.close_after_player(true);
    }
private:
    RollingMuxedHlsSession& session_;
};
} // namespace ttnx::switch_app
#endif
