#pragma once

#include "tizentube_nx/net/http.hpp"

#include <string>

namespace ttnx::switch_app {

class CurlHttpClient final : public net::HttpClient {
public:
    explicit CurlHttpClient(std::string ca_bundle_path);
    ~CurlHttpClient() override;

    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;
    CurlHttpClient(CurlHttpClient&&) = delete;
    CurlHttpClient& operator=(CurlHttpClient&&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::string& initialization_error() const noexcept { return initialization_error_; }

    net::HttpResult perform(const net::HttpRequest& request) override;

private:
    std::string ca_bundle_path_;
    std::string initialization_error_;
    bool sockets_ready_{false};
    bool curl_ready_{false};
    bool ready_{false};
};

}  // namespace ttnx::switch_app
