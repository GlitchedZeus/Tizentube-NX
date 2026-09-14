#include "tizentube_nx/net/service_init.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

}  // namespace

int main() {
    using ttnx::net::ServiceOwnership;
    using ttnx::net::classify_non_refcounted_service_init;
    using ttnx::net::classify_refcounted_service_init;

    constexpr std::uint32_t kAlreadyInitialized = 0x00abc123U;
    constexpr std::uint32_t kOtherFailure = 0x00123456U;

    const auto socket_owned = classify_non_refcounted_service_init(0, kAlreadyInitialized);
    expect(socket_owned.usable(), "socket init success is usable");
    expect(socket_owned.ownership == ServiceOwnership::Owned,
           "socket init success is owned by this caller");
    expect(socket_owned.cleanup_required(), "owned socket init requires cleanup");

    const auto socket_existing = classify_non_refcounted_service_init(
        kAlreadyInitialized, kAlreadyInitialized);
    expect(socket_existing.usable(), "AlreadyInitialized socket environment is usable");
    expect(socket_existing.ownership == ServiceOwnership::Borrowed,
           "AlreadyInitialized socket environment is borrowed");
    expect(!socket_existing.cleanup_required(),
           "borrowed socket environment must not be cleaned up by this caller");

    const auto socket_failure = classify_non_refcounted_service_init(
        kOtherFailure, kAlreadyInitialized);
    expect(!socket_failure.usable(), "other socket init failures remain unusable");
    expect(socket_failure.ownership == ServiceOwnership::Unavailable,
           "other socket init failures have no ownership");
    expect(!socket_failure.cleanup_required(),
           "failed socket init has no cleanup obligation");

    const auto ssl_success = classify_refcounted_service_init(0);
    expect(ssl_success.usable(), "reference-counted SSL init success is usable");
    expect(ssl_success.ownership == ServiceOwnership::Owned,
           "successful SSL init owns one matching cleanup reference");
    expect(ssl_success.cleanup_required(),
           "successful SSL init must release its acquired reference");

    const auto ssl_failure = classify_refcounted_service_init(kOtherFailure);
    expect(!ssl_failure.usable(), "SSL init failure remains unusable");
    expect(!ssl_failure.cleanup_required(), "failed SSL init has no cleanup obligation");

    const auto unexpected_ssl_already = classify_refcounted_service_init(kAlreadyInitialized);
    expect(!unexpected_ssl_already.usable(),
           "SSL does not borrow on AlreadyInitialized under ServiceGuard semantics");
    expect(!unexpected_ssl_already.cleanup_required(),
           "unexpected SSL AlreadyInitialized result is not cleaned up");

    if (failures == 0) {
        std::cout << "All native service ownership tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
