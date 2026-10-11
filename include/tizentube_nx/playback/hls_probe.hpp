#pragma once
#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
#include "tizentube_nx/playback/vision_hls.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <optional>
#include <string_view>

namespace ttnx::playback {

enum class HlsProbeError {
    None,
    SourceUnavailable,
    ManifestTransport,
    ManifestStatus,
    ManifestRejected,
    VariantUnavailable,
    VideoSegmentsRejected,
    AudioSegmentsRejected,
    InvalidDuration,
};

struct HlsProbePlan {
    HlsPlayerSource source;
    HlsVariant selected;
    HlsMediaPlaylist video_segments;
    std::optional<HlsMediaPlaylist> audio_segments;
};

// A real end-to-end SOURCE/PLAYLIST proof, not video playback.
// The two clients are deliberately distinct: accepted M2 WEB-only guest
// connection and an opt-in reviewed media HTTP client. All network I/O is
// caller-scheduled on a worker (never on the Borealis UI thread).
class HlsSourceProbe {
public:
    HlsSourceProbe(net::HttpClient& youtube_guest_http,
                   net::HttpClient& media_http,
                   const youtube::GuestSession& guest)
        : guest_http_(youtube_guest_http), media_http_(media_http), guest_(guest) {}

    [[nodiscard]] std::optional<HlsProbePlan> inspect(
        std::string_view normal_video_id, unsigned max_height=720,
        unsigned max_fps=30);
    [[nodiscard]] HlsProbeError last_error() const noexcept { return last_error_; }

private:
    [[nodiscard]] std::optional<std::string> fetch_playlist(
        std::string_view url, bool is_master);
    net::HttpClient& guest_http_;
    net::HttpClient& media_http_;
    const youtube::GuestSession& guest_;
    HlsProbeError last_error_{HlsProbeError::None};
};

} // namespace ttnx::playback
