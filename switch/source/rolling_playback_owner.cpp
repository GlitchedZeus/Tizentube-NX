#if defined(TTNX_M4_ROLLING_STREAM) || defined(TTNX_M4_ROLLING_HOST_TEST)
#include "rolling_playback_owner.hpp"
#include <utility>

namespace ttnx::switch_app {
RollingPlaybackOwner::~RollingPlaybackOwner() noexcept {
    worker_.request_stop();
    worker_.join_downloads();
    // The owner may be destroyed only after its mpv player. Its lease must
    // outlive the worker and any decoder in normal operation. On incorrect
    // teardown we deliberately preserve SD rather than deleting live files.
    lease_.release();
}
bool RollingPlaybackOwner::start(std::unique_ptr<Job> job) {
    if(phase_!=Phase::Idle || !job || playback::HlsPlaybackLease::is_active())
        return false;
    job_=std::move(job);
    if(!worker_.start(*job_)) {job_.reset();return false;}
    phase_=Phase::Downloading;
    joined_=decoder_ever_opened_=decoder_closed_=false;
    return true;
}
RollingHlsWorker::Snapshot RollingPlaybackOwner::snapshot() const noexcept {
    return worker_.snapshot();
}
bool RollingPlaybackOwner::begin_decoder() {
    if(phase_!=Phase::Downloading || joined_ || decoder_ever_opened_)return false;
    const auto s=worker_.snapshot();
    if(!s.prefix_ready || s.state==RollingHlsWorker::State::Failed ||
       s.state==RollingHlsWorker::State::StopRequested ||
       s.state==RollingHlsWorker::State::Joined ||
       s.state==RollingHlsWorker::State::Cleared)return false;
    // The handshake refuses to open a player while a worker deletion is
    // executing, and disables all future retirements before libmpv sees the
    // growing manifest. No speculative FD-release or timestamp heuristics.
    if(!worker_.enter_player_mode())return false;
    if(!lease_.acquire())return false;
    decoder_ever_opened_=true;
    phase_=Phase::PlayerOpen;
    return true;
}
void RollingPlaybackOwner::request_stop() noexcept {
    if(phase_!=Phase::Idle) {
        worker_.request_stop();
        if(phase_!=Phase::DecoderClosed && phase_!=Phase::FailedCleanup)
            phase_=Phase::Draining;
    }
}
void RollingPlaybackOwner::join_downloads() noexcept {
    if(phase_==Phase::Idle)return;
    worker_.request_stop();
    worker_.join_downloads();
    joined_=true;
    if(phase_==Phase::Downloading || phase_==Phase::PlayerOpen)
        phase_=Phase::Draining;
}
bool RollingPlaybackOwner::notify_decoder_destroyed() noexcept {
    if(!decoder_ever_opened_ || decoder_closed_ || !joined_ ||
       (phase_!=Phase::Draining && phase_!=Phase::PlayerOpen))return false;
    lease_.release();
    decoder_closed_=true;
    phase_=Phase::DecoderClosed;
    return true;
}
bool RollingPlaybackOwner::cleanup() noexcept {
    if(!job_ || !joined_ || lease_.owned() ||
       (decoder_ever_opened_ && !decoder_closed_) ||
       (phase_!=Phase::Draining && phase_!=Phase::DecoderClosed &&
        phase_!=Phase::FailedCleanup))return false;
    if(!worker_.cleanup_after_decoder_closed()) {
        phase_=Phase::FailedCleanup;
        return false;
    }
    job_.reset();
    phase_=Phase::Idle;
    joined_=false;
    decoder_ever_opened_=decoder_closed_=false;
    return true;
}
} // namespace ttnx::switch_app
#endif
