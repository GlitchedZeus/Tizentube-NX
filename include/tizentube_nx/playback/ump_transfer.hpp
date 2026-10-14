#pragma once
#include "tizentube_nx/net/http.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace ttnx::playback {
enum class UmpTransferFailure {
    None, InvalidUrlOrRange, TransportRejected, HttpRejected,
    InvalidEnvelope, MissingMedia,
};
struct UmpTransferResult {
    UmpTransferFailure failure{UmpTransferFailure::TransportRejected};
    std::vector<std::uint8_t> bytes;
    long http_status{0};
    [[nodiscard]] bool ok() const noexcept { return failure==UmpTransferFailure::None; }
};
// The transport is separately reviewed and checked again at the actual
// socket pre-DNS boundary. Never use the M2 browse-only HTTP client.
class UmpTransferReader {
public:
    explicit UmpTransferReader(net::HttpClient& http): http_(http) {}
    [[nodiscard]] UmpTransferResult read(
        const std::string& signed_url, std::uint64_t start,
        std::uint64_t resource_size, int request_number);
private:
    net::HttpClient& http_;
};
} // namespace ttnx::playback
