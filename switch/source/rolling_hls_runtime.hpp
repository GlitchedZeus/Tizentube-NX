#pragma once
#if defined(TTNX_M4_ROLLING_STREAM) || defined(TTNX_M4_ROLLING_HOST_TEST)
#include "rolling_hls_feed.hpp"
#include "rolling_hls_native_job.hpp"
#include "rolling_playback_owner.hpp"
#include "tizentube_nx/playback/hls_segment.hpp"
#include <memory>

namespace ttnx::switch_app {
// The full opt-in native streaming producer and SD-store lifetime in one
// object. Every field is owned (except the app-lifetime HTTP service), so no
// worker can outlive the HLS reader, coordinator, playlist or SD sink.
//
// The accepted M3 renderer uses its own HlsPlaybackLease. This runtime's
// decoder lease must be passed to an M4-aware player before activation;
// therefore no main-menu call path exists yet. Never acquire TWO leases.
class RollingHlsRuntime final {
public:
    explicit RollingHlsRuntime(net::HttpClient& http)
        : reader_(http),feed_(reader_,sink_) {}
    RollingHlsRuntime(const RollingHlsRuntime&)=delete;
    RollingHlsRuntime& operator=(const RollingHlsRuntime&)=delete;
    ~RollingHlsRuntime() = default;

    [[nodiscard]] bool begin(const playback::HlsProbePlan& plan) {
        if(active_ || !feed_.begin(plan))return false;
        auto job=std::make_unique<RollingMuxedHlsWorkerJob>(feed_);
        if(!owner_.start(std::move(job))) {
            // No reader may hold this new stream: begin() disallows other
            // decoder leases and the worker never started successfully.
            (void)feed_.close_after_player(true);
            return false;
        }
        active_=true;
        return true;
    }
    [[nodiscard]] RollingHlsWorker::Snapshot snapshot() const noexcept {
        return owner_.snapshot();
    }
    // The caller must install a player that shares this runtime's lease,
    // rather than separately calling CachedMpv::ready_lease_.acquire().
    [[nodiscard]] bool prepare_decoder() {return active_ && owner_.begin_decoder();}
    void request_stop() noexcept {if(active_)owner_.request_stop();}
    void join_downloads() noexcept {if(active_)owner_.join_downloads();}
    // After native mpv_render_context_free AND mpv_terminate_destroy only.
    [[nodiscard]] bool decoder_destroyed() noexcept {
        return active_ && owner_.notify_decoder_destroyed();
    }
    [[nodiscard]] bool cleanup() noexcept {
        if(!active_ || !owner_.cleanup())return false;
        active_=false;
        return true;
    }
    [[nodiscard]] bool active() const noexcept{return active_;}
    [[nodiscard]] std::uint64_t stored_media_bytes() const noexcept {
        return sink_.bytes();
    }
    [[nodiscard]] std::uint64_t peak_media_bytes() const noexcept {
        return feed_.peak_accounted_bytes();
    }
private:
    playback::HlsSegmentReader reader_;
    RollingHlsSink sink_;
    RollingMuxedHlsSession feed_;
    RollingPlaybackOwner owner_; // destructed FIRST, joins active worker
    bool active_{false};
};
} // namespace ttnx::switch_app
#endif
