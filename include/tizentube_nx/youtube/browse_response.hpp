#pragma once

#if !defined(TTNX_INTERNAL_SEARCH_PARSER) && !defined(__SWITCH__)
#error "browse_response.hpp is an internal renderer-payload parser API; use search_response.hpp / parse_scoped_search_response() instead."
#endif

#include "tizentube_nx/youtube/search_parse_result.hpp"

#include <string_view>

namespace ttnx::youtube {

// INTERNAL ONLY: parses an already-scoped Search renderer payload. It performs
// bounded traversal + renderer firewalling, but it does NOT establish the full
// HTTP response Search boundary. Network-facing code must call
// parse_scoped_search_response() instead.
[[nodiscard]] BrowseResponseParseResult parse_search_response(
    std::string_view response_body,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
