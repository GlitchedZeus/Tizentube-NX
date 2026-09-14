#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::core {

// Renderer-agnostic result model exposed to future UI/controller code. YouTube
// renderer names and raw JSON never cross this boundary.
enum class BrowseResultKind {
    Video,
    Channel,
    Playlist,
};

// A normalized reference only; no image network request is implied. Candidates
// are bounded/sanitized before entering BrowseResult and ordered by deterministic
// preference (known larger dimensions first, then stable URL ordering).
struct ThumbnailCandidate {
    std::string url;
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct BrowseResult {
    BrowseResultKind kind{BrowseResultKind::Video};
    std::string id;
    std::string title;

    // Shared attribution / presentation fields. Empty means YouTube omitted the
    // value or the parser could not accept it safely; callers must not invent it.
    std::string channel_name;
    std::string channel_id;

    // Preferred candidate is mirrored in thumbnail_url for compatibility with
    // existing callers. Future UI code should prefer thumbnails when it needs
    // dimensions/fallback selection.
    std::vector<ThumbnailCandidate> thumbnails;
    std::string thumbnail_url;

    std::string duration_text;
    std::optional<std::uint64_t> duration_seconds;

    std::string view_count_text;
    std::optional<std::uint64_t> view_count;
    std::string published_text;

    std::string subscriber_count_text;
    std::optional<std::uint64_t> subscriber_count;
    std::string video_count_text;
    std::optional<std::uint64_t> video_count;

    std::string accessibility_text;

    bool live{false};
    bool upcoming{false};
    std::optional<std::uint64_t> scheduled_start_time_seconds;
};

// Deterministic stable identity used for dedupe/selection. The type prefix
// prevents a channel/playlist ID collision from aliasing a video result.
[[nodiscard]] std::string browse_result_identity(const BrowseResult& result);

}  // namespace ttnx::core
