#include "tizentube_nx/net/outbound_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_blocked(std::string_view url, const char* message) {
    expect(!ttnx::net::parse_allowed_https_destination(url).has_value(), message);
}

}  // namespace

int main() {
    using namespace ttnx::net;

    expect(is_allowed_outbound_host("www.youtube.com"),
           "M2 YouTube host is explicitly allowlisted");
    expect(is_allowed_outbound_host("WWW.YOUTUBE.COM"),
           "host policy is case-insensitive");

    // Nintendo is a hard deny, independent of 90DNS.
    expect(is_nintendo_host("nintendo.com"), "nintendo.com recognized as Nintendo");
    expect(is_nintendo_host("accounts.nintendo.com"), "Nintendo subdomain recognized");
    expect(is_nintendo_host("nintendo.net"), "nintendo.net recognized as Nintendo");
    expect(is_nintendo_host("sun.hac.lp1.d4c.nintendo.net"),
           "deep Nintendo service subdomain recognized");
    expect(is_nintendo_host("nintendo.co.jp"), "Nintendo Japan domain recognized");
    expect(is_nintendo_host("nintendowifi.net"), "Nintendo Wi-Fi domain recognized");
    expect(is_nintendo_host("nintendo-europe.com"), "Nintendo Europe domain recognized");

    expect(!is_allowed_outbound_host("nintendo.com"), "Nintendo root host denied");
    expect(!is_allowed_outbound_host("accounts.nintendo.com"), "Nintendo subdomain denied");
    expect(!is_allowed_outbound_host("sun.hac.lp1.d4c.nintendo.net"),
           "Nintendo service host denied");

    // Default deny means non-Nintendo hosts are still blocked until deliberately added.
    expect(!is_allowed_outbound_host("example.com"), "unlisted host denied");
    expect(!is_allowed_outbound_host("youtube.com"),
           "nearby but unlisted YouTube hostname denied");
    expect(!is_allowed_outbound_host("music.youtube.com"),
           "unlisted YouTube subdomain denied");

    const auto youtube = parse_allowed_https_destination(
        "https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json");
    expect(youtube.has_value(), "exact M2 YouTube HTTPS destination accepted");
    if (youtube) {
        expect(youtube->host == "www.youtube.com", "allowed host normalized");
        expect(youtube->port == 443, "allowed destination forced to TLS port 443");
    }

    // Scheme / port / authority tricks must fail before DNS.
    expect_blocked("http://www.youtube.com/", "plain HTTP denied");
    expect_blocked("https://www.youtube.com:80/", "non-TLS port denied");
    expect_blocked("https://www.youtube.com:444/", "alternate TLS port denied");
    expect_blocked("https://127.0.0.1/", "IPv4 literal denied");
    expect_blocked("https://[::1]/", "IPv6 literal denied");
    expect_blocked("https://www.youtube.com./", "trailing-dot hostname denied");

    // Confusable/redirect-style authority tricks targeting Nintendo must not pass.
    expect_blocked("https://www.youtube.com.nintendo.net/",
                   "YouTube-looking Nintendo subdomain denied");
    expect_blocked("https://www.youtube.com@accounts.nintendo.com/",
                   "userinfo authority trick targeting Nintendo denied");
    expect_blocked("https://accounts.nintendo.com@www.youtube.com/",
                   "userinfo is forbidden even when final host looks allowed");
    expect_blocked("https://nintendo.com/?next=https://www.youtube.com/",
                   "Nintendo URL denied regardless of query text");
    expect_blocked("https://example.com/redirect?to=https://www.youtube.com/",
                   "unlisted redirector denied");

    if (failures == 0) {
        std::cout << "All outbound network policy tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
