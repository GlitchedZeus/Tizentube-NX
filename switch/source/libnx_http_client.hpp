#pragma once

#include "tizentube_nx/net/http.hpp"

#include <string>

namespace ttnx::switch_app {

// Native Switch HTTPS client backed by libnx BSD sockets + Horizon's local
// ssl service. All remote destinations are checked by the TizenTube NX
// default-deny outbound policy before DNS resolution.
class LibnxHttpClient final : public net::HttpClient {
public:
    LibnxHttpClient();
    ~LibnxHttpClient() override;

    LibnxHttpClient(const LibnxHttpClient&) = delete;
    LibnxHttpClient& operator=(const LibnxHttpClient&) = delete;

    net::HttpResult perform(const net::HttpRequest& request) override;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::string& initialization_error() const noexcept {
        return initialization_error_;
    }

private:
    bool sockets_ready_{false};
    bool ssl_ready_{false};
    bool ready_{false};
    std::string initialization_error_;
};

}  // namespace ttnx::switch_app
