#pragma once
#include "rolling_hls_worker.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <memory>

namespace ttnx::switch_app {
// Opt-in application-level lifecycle boundary. Owns the network Job so its
// lifetime cannot end while RollingHlsWorker still executes it. The accepted
// M3 player does NOT instantiate this experimental owner.
//
// Strict lifecycle: stage worker -> verified prefix -> acquire mpv lease ->
// request stop + join worker -> fully destroy mpv and render context ->
// notify_decoder_destroyed -> delete private media. No per-segment eviction
// while mpv holds a lease; mpv does not expose a trustworthy FD-release signal.
class RollingPlaybackOwner final {
public:
    using Job=RollingHlsWorker::Job;
    enum class Phase { Idle, Downloading, PlayerOpen, Draining, DecoderClosed,
                       FailedCleanup };
    RollingPlaybackOwner() = default;
    RollingPlaybackOwner(const RollingPlaybackOwner&)=delete;
    RollingPlaybackOwner& operator=(const RollingPlaybackOwner&)=delete;
    ~RollingPlaybackOwner() noexcept;

    [[nodiscard]] bool start(std::unique_ptr<Job> job);
    [[nodiscard]] RollingHlsWorker::Snapshot snapshot() const noexcept;
    [[nodiscard]] bool begin_decoder(); // only after a verified A/V prefix
    void request_stop() noexcept;
    void join_downloads() noexcept; // may wait for bounded in-flight network
    // Must be called *after* mpv_render_context_free + mpv_terminate_destroy.
    // No bool override for claimed FD releases or speculative eviction.
    [[nodiscard]] bool notify_decoder_destroyed() noexcept;
    // A closed decoder and joined downloader are prerequisites. If cleanup
    // fails, keep the owned Job for an explicit retry rather than unlinking
    // unsafe media or tearing down its backing state.
    [[nodiscard]] bool cleanup() noexcept;
    [[nodiscard]] Phase phase() const noexcept {return phase_;}
    [[nodiscard]] bool owns_decoder_lease() const noexcept {return lease_.owned();}
private:
    // Order is deliberate: worker destructs before job if a caller neglects
    // stop. Job must outlive the thread. Destructor never removes SD bytes.
    std::unique_ptr<Job> job_{};
    RollingHlsWorker worker_{};
    playback::HlsPlaybackLease lease_{};
    Phase phase_{Phase::Idle};
    bool joined_{false};
    bool decoder_ever_opened_{false};
    bool decoder_closed_{false};
};
} // namespace ttnx::switch_app
