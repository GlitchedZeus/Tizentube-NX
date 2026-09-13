#pragma once

#include "tizentube_nx/core/guest_browse.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::youtube {

struct BrowseResponseParseResult {
    std::optional<core::BrowsePage> page;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page.has_value();
    }
};

// Parses a YouTube InnerTube Search response into the renderer-neutral guest
// model, then applies the hard renderer firewall before returning anything that
// can reach UI code. The parser is bounded and fail-closed on malformed JSON.
// Unknown renderer/container shapes are ignored rather than guessed into cards.
[[nodiscard]] BrowseResponseParseResult parse_search_response(
    std::string_view response_body,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
