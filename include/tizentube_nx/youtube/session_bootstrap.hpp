#pragma once

#include "tizentube_nx/youtube/guest_api.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::youtube {

struct SessionBootstrapOptions {
    std::string language;
    std::string region;
    std::string timezone;
    std::string user_agent;
};

struct SessionBootstrapParseResult {
    std::optional<GuestSession> session;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return session.has_value();
    }
};

// Parses only the small set of fields required for anonymous guest requests
// from YouTube's sw.js_data JSPB bootstrap response. Unknown fields are skipped,
// the parser is bounded, and shape/type mismatches fail closed.
[[nodiscard]] SessionBootstrapParseResult parse_session_bootstrap(
    std::string_view response_body,
    const SessionBootstrapOptions& options = {});

}  // namespace ttnx::youtube
