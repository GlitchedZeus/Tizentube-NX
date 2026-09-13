#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::net {

// TizenTube NX runs on modded Switch consoles. Outbound networking is therefore
// deny-by-default and must never depend on 90DNS for safety.
//
// Before DNS lookup, socket creation, TLS, redirects, image loading or any other
// network action, the destination must pass this policy.

struct OutboundDestination {
    std::string host;
    unsigned short port{443};
};

[[nodiscard]] bool is_nintendo_host(std::string_view host);
[[nodiscard]] bool is_allowed_outbound_host(std::string_view host);
[[nodiscard]] std::optional<OutboundDestination> parse_allowed_https_destination(
    std::string_view url);

}  // namespace ttnx::net
