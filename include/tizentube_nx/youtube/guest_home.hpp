#pragma once

#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/core/home_model.hpp"
#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <optional>
#include <string>

namespace ttnx::youtube {

struct GuestHomeResult {
    std::optional<core::HomePage> page;
    // Sanitized public-safe diagnostic. Raw response bodies, tokens, visitor
    // identifiers and request metadata must never escape through this field.
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page.has_value();
    }
};

// Guest Home boundary shared by host tests and the Switch shell:
// validate -> exact FEwhat_to_watch request -> transport -> scoped Home parser
// -> renderer firewall -> normalized HomePage.
[[nodiscard]] GuestHomeResult execute_guest_home(
    net::HttpClient& http,
    const GuestSession& session,
    const core::GuestRequest& request,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
