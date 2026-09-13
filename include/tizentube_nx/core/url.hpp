#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::core {

// YouTube video IDs are currently 11 characters. Validation is deliberately
// strict so the app never emits malformed or attacker-controlled share URLs.
bool is_valid_video_id(std::string_view video_id);

// Generates the only share format TizenTube NX should emit.
std::optional<std::string> canonical_watch_url(std::string_view video_id);

// Extracts a video ID from common YouTube video URL forms. Shorts URLs are
// intentionally rejected: Shorts are not part of TizenTube NX.
std::optional<std::string> extract_video_id(std::string_view url);

// Convenience wrapper for pasted/shared URLs. Returns nullopt if the URL is
// unsupported, malformed, or points at /shorts/.
std::optional<std::string> canonicalize_youtube_video_url(std::string_view url);

bool is_shorts_url(std::string_view url);

}  // namespace ttnx::core
