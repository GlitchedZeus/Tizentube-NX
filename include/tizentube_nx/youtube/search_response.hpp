#pragma once

#include "tizentube_nx/youtube/browse_response.hpp"

#include <string_view>

namespace ttnx::youtube {

// Full Search-response boundary used by network-facing code. It first restricts
// renderer traversal to recognized Search primary/continuation payloads, then
// delegates to the bounded renderer parser + firewall. This prevents unrelated
// topbar/secondary renderer-shaped data from becoming Search cards.
[[nodiscard]] BrowseResponseParseResult parse_scoped_search_response(
    std::string_view response_body,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
