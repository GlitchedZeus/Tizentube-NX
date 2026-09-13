#include "tizentube_nx/net/outbound_policy.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>

namespace ttnx::net {
namespace {

std::string lowercase_ascii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool valid_dns_name(std::string_view host) {
    if (host.empty() || host.size() > 253) return false;
    if (host.front() == '.' || host.back() == '.') return false;

    std::size_t label_start = 0;
    while (label_start < host.size()) {
        const auto dot = host.find('.', label_start);
        const auto label_end = dot == std::string_view::npos ? host.size() : dot;
        const auto length = label_end - label_start;
        if (length == 0 || length > 63) return false;
        if (host[label_start] == '-' || host[label_end - 1] == '-') return false;

        for (std::size_t i = label_start; i < label_end; ++i) {
            const unsigned char c = static_cast<unsigned char>(host[i]);
            if (!(std::isalnum(c) || c == '-')) return false;
        }

        if (dot == std::string_view::npos) break;
        label_start = dot + 1;
    }
    return true;
}

bool host_is_or_subdomain_of(std::string_view host, std::string_view suffix) {
    if (host == suffix) return true;
    if (host.size() <= suffix.size()) return false;
    const auto offset = host.size() - suffix.size();
    return host.substr(offset) == suffix && host[offset - 1] == '.';
}

}  // namespace

bool is_nintendo_host(std::string_view host) {
    const auto normalized = lowercase_ascii(host);

    // Explicit hard-deny list. This is intentionally broader than the current
    // M2 allowlist so future feature work cannot accidentally add a Nintendo
    // endpoint without tripping policy tests first.
    return host_is_or_subdomain_of(normalized, "nintendo.com") ||
           host_is_or_subdomain_of(normalized, "nintendo.net") ||
           host_is_or_subdomain_of(normalized, "nintendo.co.jp") ||
           host_is_or_subdomain_of(normalized, "nintendowifi.net") ||
           host_is_or_subdomain_of(normalized, "nintendo-europe.com");
}

bool is_allowed_outbound_host(std::string_view host) {
    const auto normalized = lowercase_ascii(host);
    if (!valid_dns_name(normalized)) return false;
    if (is_nintendo_host(normalized)) return false;

    // M2 default-deny allowlist. Additions require code review + host-policy
    // tests. Do not use wildcard domains here.
    return normalized == "www.youtube.com";
}

std::optional<OutboundDestination> parse_allowed_https_destination(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size()) return std::nullopt;

    for (std::size_t i = 0; i < scheme.size(); ++i) {
        const unsigned char actual = static_cast<unsigned char>(url[i]);
        if (std::tolower(actual) != scheme[i]) return std::nullopt;
    }

    const auto authority_start = scheme.size();
    const auto authority_end = url.find_first_of("/?#", authority_start);
    const auto authority = url.substr(
        authority_start,
        authority_end == std::string_view::npos ? std::string_view::npos
                                                : authority_end - authority_start);

    if (authority.empty()) return std::nullopt;
    if (authority.find('@') != std::string_view::npos) return std::nullopt;
    if (authority.front() == '[') return std::nullopt; // no IP literals/IPv6.

    std::string_view host = authority;
    unsigned short port = 443;

    if (const auto colon = authority.rfind(':'); colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        const auto port_text = authority.substr(colon + 1);
        if (port_text.empty()) return std::nullopt;

        unsigned long value = 0;
        for (const char c : port_text) {
            if (c < '0' || c > '9') return std::nullopt;
            value = value * 10 + static_cast<unsigned long>(c - '0');
            if (value > std::numeric_limits<unsigned short>::max()) return std::nullopt;
        }
        if (value != 443) return std::nullopt;
        port = static_cast<unsigned short>(value);
    }

    const auto normalized = lowercase_ascii(host);
    if (!is_allowed_outbound_host(normalized)) return std::nullopt;

    return OutboundDestination{normalized, port};
}

}  // namespace ttnx::net
