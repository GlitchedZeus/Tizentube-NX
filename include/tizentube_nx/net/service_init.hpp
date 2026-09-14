#pragma once

#include <cstdint>

namespace ttnx::net {

// Ownership here means responsibility for the matching cleanup call, not
// necessarily exclusive ownership of the underlying Horizon service.
enum class ServiceOwnership {
    Unavailable,
    Owned,
    Borrowed,
};

struct ServiceInitDecision {
    ServiceOwnership ownership{ServiceOwnership::Unavailable};

    [[nodiscard]] constexpr bool usable() const noexcept {
        return ownership != ServiceOwnership::Unavailable;
    }

    [[nodiscard]] constexpr bool cleanup_required() const noexcept {
        return ownership == ServiceOwnership::Owned;
    }
};

// For non-reference-counted runtime resources such as libnx's socket devoptab:
// success means this caller owns the matching cleanup; the one exact
// AlreadyInitialized result means a usable pre-existing environment is borrowed.
// Every other non-zero result remains a failure.
[[nodiscard]] constexpr ServiceInitDecision classify_non_refcounted_service_init(
    std::uint32_t result,
    std::uint32_t already_initialized_result) noexcept {
    if (result == 0) return {ServiceOwnership::Owned};
    if (result == already_initialized_result) return {ServiceOwnership::Borrowed};
    return {ServiceOwnership::Unavailable};
}

// libnx ServiceGuard-backed services (including ssl) are reference-counted.
// A successful initialize call acquires one cleanup obligation even if another
// component already has the service initialized; any non-zero result is a real
// failure for this caller.
[[nodiscard]] constexpr ServiceInitDecision classify_refcounted_service_init(
    std::uint32_t result) noexcept {
    return result == 0
        ? ServiceInitDecision{ServiceOwnership::Owned}
        : ServiceInitDecision{ServiceOwnership::Unavailable};
}

}  // namespace ttnx::net
