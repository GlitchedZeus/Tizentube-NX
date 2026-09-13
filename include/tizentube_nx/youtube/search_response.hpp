#pragma once

#include "tizentube_nx/youtube/search_parse_result.hpp"

// Core parser implementation files are compiled with this definition so the
// scoped boundary can delegate to the low-level renderer walker. The Switch
// target is one monolithic internal build, so __SWITCH__ grants the same access.
// Ordinary host/application callers never receive the lower-level declaration.
#if defined(TTNX_INTERNAL_SEARCH_PARSER) || defined(__SWITCH__)
#include "tizentube_nx/youtube/browse_response.hpp"
#endif

#include <string_view>

namespace ttnx::youtube {

// The only Search-response parser API for network-facing/application callers.
// It restricts traversal to recognized Search primary/continuation payloads,
// then delegates those payloads to the bounded renderer parser + firewall.
[[nodiscard]] BrowseResponseParseResult parse_scoped_search_response(
    std::string_view response_body,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
