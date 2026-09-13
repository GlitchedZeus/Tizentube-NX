#pragma once

#include <string>
#include <string_view>

namespace ttnx::core {

// Renderer-agnostic result model exposed to future UI/controller code. YouTube
// renderer names and raw JSON never cross this boundary.
enum class BrowseResultKind {
    Video,
    Channel,
    Playlist,
};

struct BrowseResult {
    BrowseResultKind kind{BrowseResultKind::Video};
    std::string id;
    std::string title;

    // Shared attribution / presentation fields. Empty means YouTube omitted the
    // value or the parser could not accept it safely; callers must not invent it.
    std::string channel_name;
    std::string channel_id;
    std::string thumbnail_url;
    std::string duration_text;
    std::string view_count_text;
    std::string published_text;
    std::string subscriber_count_text;
    std::string video_count_text;
    std::string accessibility_text;

    bool live{false};
    bool upcoming{false};
};

// Deterministic stable identity used for dedupe/selection. The type prefix
// prevents a channel/playlist ID collision from aliasing a video result.
[[nodiscard]] std::string browse_result_identity(const BrowseResult& result);

}  // namespace ttnx::core
