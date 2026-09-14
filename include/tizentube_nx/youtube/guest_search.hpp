#pragma once

#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/core/search_error.hpp"
#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <optional>
#include <string>

namespace ttnx::youtube {

struct GuestSearchResult {
    std::optional<core::BrowsePage> page;
    core::SearchErrorCode error_code{core::SearchErrorCode::None};
    // Technical diagnostic for logs/tests only. It must already be sanitized by
    // the producer; future UI code should use error_code + search_error_message().
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page.has_value();
    }
};

// End-to-end Search boundary shared by host tests and the future Switch UI:
// validate request -> build InnerTube POST -> transport -> bounded parser ->
// renderer firewall. The Switch shell does not call this until guest bootstrap
// has been accepted on real hardware.
[[nodiscard]] GuestSearchResult execute_guest_search(
    net::HttpClient& http,
    const GuestSession& session,
    const core::GuestRequest& request,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
