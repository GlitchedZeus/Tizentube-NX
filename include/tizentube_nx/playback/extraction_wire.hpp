#pragma once

#include "tizentube_nx/playback/playback.hpp"

#include <optional>
#include <string_view>

namespace ttnx::playback {

// Strict parser for the *normalized*, local research resolver response.
// Not a general yt-dlp or YouTube JSON parser. Requires fixed reviewed fields,
// rejects duplicate/unknown fields and caps all allocation and array sizes.
// Calling this function never authorizes a network destination.
[[nodiscard]] std::optional<ExtractionResult> parse_extraction_wire_response(
    std::string_view json);

} // namespace ttnx::playback
