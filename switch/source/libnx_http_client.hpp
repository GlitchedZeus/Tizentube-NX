#pragma once

#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/net/service_init.hpp"

#include <string>

namespace ttnx::switch_app {

enum class NativeInitFailure {
    None,
    Socket,
    Ssl,
};

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
    [[nodiscard]] NativeInitFailure initialization_failure() const noexcept {
        return initialization_failure_;
    }
    [[nodiscard]] net::ServiceOwnership socket_ownership() const noexcept {
        return socket_ownership_;
    }
    [[nodiscard]] net::ServiceOwnership ssl_ownership() const noexcept {
        return ssl_ownership_;
    }
    [[nodiscard]] const std::string& initialization_error() const noexcept {
        return initialization_error_;
    }
    [[nodiscard]] std::string service_status() const;

private:
    net::ServiceOwnership socket_ownership_{net::ServiceOwnership::Unavailable};
    net::ServiceOwnership ssl_ownership_{net::ServiceOwnership::Unavailable};
    NativeInitFailure initialization_failure_{NativeInitFailure::None};
    bool ready_{false};
    std::string initialization_error_;
};

}  // namespace ttnx::switch_app
