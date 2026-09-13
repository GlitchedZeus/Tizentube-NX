#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::core {

// Stable YouTube identifiers accepted by local routing/sharing code. Validation
// is deliberately strict so malformed or attacker-controlled text cannot become
// a future playback/share target.
[[nodiscard]] bool is_valid_video_id(std::string_view video_id);
[[nodiscard]] bool is_valid_channel_id(std::string_view channel_id);
[[nodiscard]] bool is_valid_playlist_id(std::string_view playlist_id);

// Canonical clean URLs emitted by TizenTube NX. These helpers never preserve
// tracking/query garbage from source URLs.
[[nodiscard]] std::optional<std::string> canonical_watch_url(std::string_view video_id);
[[nodiscard]] std::optional<std::string> canonical_channel_url(std::string_view channel_id);
[[nodiscard]] std::optional<std::string> canonical_playlist_url(std::string_view playlist_id);

// Extracts a normal-video ID from supported YouTube URL forms. Shorts/reel URLs
// are intentionally rejected and can never be converted into normal videos.
[[nodiscard]] std::optional<std::string> extract_video_id(std::string_view url);

// Convenience wrapper for pasted/shared URLs. Returns nullopt for unsupported,
// malformed, ambiguous, oversized, Shorts/reel, or non-YouTube URLs.
[[nodiscard]] std::optional<std::string> canonicalize_youtube_video_url(std::string_view url);

[[nodiscard]] bool is_shorts_url(std::string_view url);

}  // namespace ttnx::core
