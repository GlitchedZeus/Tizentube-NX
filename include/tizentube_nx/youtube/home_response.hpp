#pragma once

#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/home_model.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::youtube {

struct HomeResponseParseResult {
    std::optional<core::HomePage> page;
    std::string error;
    // Privacy-safe, bounded structural diagnostics only. This must never contain
    // response values such as titles, IDs, tokens, visitor data or URLs.
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page.has_value();
    }
};

// Network-facing Home/browse parser boundary. Only recognized Home tab content
// and continuation-item arrays are delegated to the bounded renderer walker;
// unrelated topbar/menu/command metadata is never recursively scanned.
[[nodiscard]] HomeResponseParseResult parse_scoped_home_response(
    std::string_view response_body,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
