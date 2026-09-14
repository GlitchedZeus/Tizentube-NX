#pragma once

#include "tizentube_nx/net/http.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::youtube {

struct GuestSession {
    std::string api_version{"v1"};
    std::string client_name{"WEB"};
    std::string client_name_id;
    std::string client_version;
    std::string visitor_data;
    std::string language{"en"};
    std::string region{"US"};
    std::string timezone{"UTC"};
    std::string user_agent;

    [[nodiscard]] bool usable() const noexcept {
        return !api_version.empty() && !client_name.empty() &&
               !client_version.empty() && !visitor_data.empty();
    }
};

[[nodiscard]] net::HttpRequest make_session_bootstrap_request(
    std::string_view accept_language,
    std::string_view timezone,
    std::string_view user_agent,
    std::string_view visitor_cookie_id);

[[nodiscard]] std::optional<net::HttpRequest> make_search_request(
    const GuestSession& session,
    std::string_view query,
    std::string_view continuation = {});

[[nodiscard]] std::optional<net::HttpRequest> make_browse_request(
    const GuestSession& session,
    std::string_view browse_id,
    std::string_view continuation = {});

[[nodiscard]] std::optional<net::HttpRequest> make_home_request(
    const GuestSession& session,
    std::string_view continuation = {});

}  // namespace ttnx::youtube
