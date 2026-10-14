#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::playback {

// This is a pure source-routing model. It does NOT start sockets or authorize
// network access. M2 browsing policy remains frozen until separately reviewed.
enum class VideoDelivery { None, Hls, SeparateAv, ProgressiveMp4 };
enum class SourceRouteFailure {
    None, InvalidId, ShortsForbidden, RestrictedOrDrm, InvalidDuration,
    NoReviewedMediaSource,
};
struct SourceCandidate {
    VideoDelivery delivery{VideoDelivery::None};
    std::string video_url;     // HLS manifest, UMP video, or direct MP4
    std::string audio_url;     // required for separate video/audio route
    unsigned height{0};
    unsigned fps{0};
    bool h264{false};
    bool aac{false};
    bool drm{false};
    bool requires_auth{false}; // no auth or cookie bypass in the guest lane
};
struct VideoSourceSet {
    std::string video_id;
    std::uint64_t duration_ms{0};
    bool is_short{false};
    bool is_live{false};
    bool requires_auth{false};
    std::vector<SourceCandidate> candidates;
};
struct PlannedSource {
    VideoDelivery delivery{VideoDelivery::None};
    std::string video_url;
    std::string audio_url;
    unsigned height{0};
    unsigned fps{0};
    std::uint64_t duration_ms{0};
};
struct RouteResult {
    std::optional<PlannedSource> planned;
    SourceRouteFailure failure{SourceRouteFailure::NoReviewedMediaSource};
};

// Only accepted for explicit, independently reviewed M3 media-plane traffic.
// An HLS manifest parser and every referenced segment need separate host
// checks; libmpv may NOT open these URLs directly until its internal
// networking and redirect behavior are reviewed.
[[nodiscard]] bool is_reviewable_hls_manifest_url(std::string_view url);

// Prefer HLS, then two explicitly paired H264+AAC video/audio streams,
// then the lower-resolution self-contained progressive MP4.
// This selector never treats successful metadata resolution as proof that
// an upstream URL can actually be fetched/decoded; preflight is separate.
[[nodiscard]] RouteResult choose_source_route(const VideoSourceSet& sources,
                                               unsigned max_height=720,
                                               unsigned max_fps=30);
} // namespace ttnx::playback
