#pragma once

#include "tizentube_nx/core/channel_model.hpp"
#include "tizentube_nx/core/content_filter.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::youtube {

struct ChannelResponseParseResult {
    std::optional<core::ChannelPage> page;
    std::string error;
    // Bounded structural family names/counts only. Never raw response values,
    // titles, IDs, continuation tokens, visitor data, cookies or URLs.
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return page.has_value(); }
};

// Network-facing Channel parser boundary. Only the provider-selected browse tab
// and explicitly reviewed structural wrappers are traversed. Unknown/blocked
// wrappers stay opaque and unselected tabs/topbar/menu metadata remain out of scope.
[[nodiscard]] ChannelResponseParseResult parse_scoped_channel_response(
    std::string_view response_body,
    std::string_view expected_channel_id,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
