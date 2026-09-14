#pragma once

#include "tizentube_nx/core/channel_model.hpp"
#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <optional>
#include <string>

namespace ttnx::youtube {

struct GuestChannelResult {
    std::optional<core::ChannelPage> page;
    std::string error;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return page.has_value(); }
};

// Explicit first-page Channel boundary. Channel continuation remains disabled
// until the first-page shape is physically accepted.
[[nodiscard]] GuestChannelResult execute_guest_channel(
    net::HttpClient& http,
    const GuestSession& session,
    const core::GuestRequest& request,
    const core::FilterPolicy& policy = {});

}  // namespace ttnx::youtube
