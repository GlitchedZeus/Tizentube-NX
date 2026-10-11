#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::playback {

struct HlsVariant {
    std::string playlist_url;
    std::string audio_playlist_url; // optional separate AAC audio rendition
    unsigned width{0};
    unsigned height{0};
    unsigned fps{0};
    std::uint64_t bandwidth{0};
};
struct HlsMaster {
    std::vector<HlsVariant> compatible;
};

// Manifest and every referenced resource must pass independent domain,
// HTTPS, path, size and no-redirect checks. Parsing does not grant network
// permission or allow libmpv's internal HTTP stack to make requests.
[[nodiscard]] bool is_reviewable_hls_child_url(std::string_view url);
[[nodiscard]] std::optional<HlsMaster> parse_hls_master_playlist(
    std::string_view manifest, std::string_view master_url);
[[nodiscard]] std::optional<HlsVariant> choose_hls_variant(
    const HlsMaster& playlist, unsigned max_height=720,
    unsigned max_fps=30);

struct HlsSegment {
    std::string url;
    std::uint64_t duration_ms{0};
};
struct HlsMediaPlaylist {
    std::optional<std::string> init_segment_url;
    std::vector<HlsSegment> segments;
    std::uint64_t total_duration_ms{0};
};
// Initial VOD-only segment model, not a streaming client. Encryption,
// discontinuities, live manifests, and byte-range dependency are not accepted
// until implemented and independently tested.
[[nodiscard]] std::optional<HlsMediaPlaylist> parse_hls_media_playlist(
    std::string_view manifest, std::string_view playlist_url);
} // namespace ttnx::playback
