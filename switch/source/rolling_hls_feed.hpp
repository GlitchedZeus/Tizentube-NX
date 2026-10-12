#pragma once
#include "rolling_hls_sink.hpp"
#include "tizentube_nx/playback/hls_cache.hpp"
#include "tizentube_nx/playback/rolling_stream_coordinator.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ttnx::switch_app {

// Opt-in *worker-owned* transfer-to-local-HLS bridge.
// Only muxed H.264/AAC MPEG-TS VOD: independent audio/fMP4 still requires
// an explicitly reviewed streaming integration. No libmpv network access.
// The accepted M3 player does not instantiate this experimental class.
class RollingMuxedHlsSession final {
public:
    enum class State { Idle, Fetching, PrefixReady, Waiting, Complete, Failed, Cancelled };
    enum class Error {
        None, InvalidPlan, UnableToStage, SegmentHttp, Codec,
        MediaWrite, Accounting, Manifest
    };
    RollingMuxedHlsSession(playback::HlsSegmentReader& reader,
                           RollingHlsSink& sink) noexcept
        : reader_(reader), sink_(sink) {}
    RollingMuxedHlsSession(const RollingMuxedHlsSession&)=delete;
    RollingMuxedHlsSession& operator=(const RollingMuxedHlsSession&)=delete;
    [[nodiscard]] bool begin(const playback::HlsProbePlan& plan);
    // One HTTPS fetch maximum. Call on the worker, never the Borealis tick.
    [[nodiscard]] State pump();
    // The decoder must have closed all FDs below first_retained_index.
    // The caller must prove this via its playback lifecycle, never guess.
    [[nodiscard]] bool retire_before(std::size_t first_retained_index,
                                     std::uint64_t playhead_ms,
                                     bool decoder_released);
    // Call only after joining worker and completely closing libmpv.
    [[nodiscard]] bool close_after_player(bool decoder_closed) noexcept;
    [[nodiscard]] State state() const noexcept {return state_;}
    [[nodiscard]] Error error() const noexcept {return error_;}
    [[nodiscard]] bool prefix_prepared() const noexcept {
        return state_==State::PrefixReady || state_==State::Waiting ||
               state_==State::Complete;
    }
    [[nodiscard]] std::size_t downloaded_segments() const noexcept {return next_;}
    [[nodiscard]] std::uint64_t stored_bytes() const noexcept {return sink_.bytes();}
    [[nodiscard]] std::uint64_t peak_accounted_bytes() const noexcept {
        return coordinator_.peak_media_bytes();
    }
private:
    [[nodiscard]] State failure(Error error) noexcept {
        error_=error;
        state_=State::Failed;
        return state_;
    }
    playback::HlsSegmentReader& reader_;
    RollingHlsSink& sink_;
    playback::RollingStreamCoordinator coordinator_{};
    std::vector<playback::HlsSegment> segments_{};
    std::vector<std::uint64_t> ends_ms_{};
    std::size_t next_{0},first_retained_{0};
    State state_{State::Idle};
    Error error_{Error::None};
};
} // namespace ttnx::switch_app
