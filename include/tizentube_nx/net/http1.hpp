#pragma once

#include "tizentube_nx/net/http.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ttnx::net {

struct HttpsUrl {
    std::string host;
    std::string target;
    unsigned short port{443};
};

struct Http1ParseResult {
    std::optional<HttpResponse> response;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return response.has_value();
    }
};

[[nodiscard]] std::optional<HttpsUrl> parse_https_url(std::string_view url);
[[nodiscard]] std::optional<std::string> build_http1_request(
    const HttpRequest& request,
    const HttpsUrl& url);
[[nodiscard]] Http1ParseResult parse_http1_response(
    std::string_view raw_response,
    std::size_t max_body_bytes = 16 * 1024 * 1024);
[[nodiscard]] std::optional<std::string_view> find_header_value(
    const HttpResponse& response,
    std::string_view name);

}  // namespace ttnx::net
