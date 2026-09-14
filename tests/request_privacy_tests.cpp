#include "tizentube_nx/net/diagnostics.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include "tizentube_nx/youtube/guest_search.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::optional<std::string_view> header_value(
    const std::vector<ttnx::net::HttpHeader>& headers,
    std::string_view name) {
    for (const auto& header : headers) {
        if (header.name == name) return header.value;
    }
    return std::nullopt;
}

ttnx::youtube::GuestSession session() {
    ttnx::youtube::GuestSession value;
    value.client_name = "WEB";
    value.client_name_id = "1";
    value.client_version = "2.20260901.00.00";
    value.visitor_data = "visitor-private-value";
    value.language = "en-CA";
    value.region = "CA";
    value.timezone = "America/Toronto";
    value.user_agent = "TizenTube-NX-Test/1.0";
    return value;
}

class SensitiveErrorClient final : public ttnx::net::HttpClient {
public:
    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest&) override {
        ttnx::net::HttpResult result;
        result.error = "transport failed; X-Goog-Visitor-Id=visitor-private-value; continuation=SECRET";
        return result;
    }
};

}  // namespace

int main() {
    using namespace ttnx::youtube;

    const auto guest = session();
    const auto request = make_search_request(guest, "minimal request");
    expect(request.has_value(), "bounded guest Search request builds");
    if (request) {
        expect(request->url == "https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json",
               "Search remains on the exact approved www.youtube.com endpoint");
        expect(header_value(request->headers, "X-Goog-Visitor-Id") == "visitor-private-value",
               "known guest visitor header is retained for protocol compatibility");
        expect(header_value(request->headers, "X-Youtube-Client-Version") == "2.20260901.00.00",
               "known client-version header is retained");
        expect(header_value(request->headers, "Origin") == "https://www.youtube.com",
               "known same-origin WEB header is retained");
        expect(header_value(request->headers, "Referer") == "https://www.youtube.com/",
               "known same-origin WEB referer is retained");
        expect(header_value(request->headers, "User-Agent") == "TizenTube-NX-Test/1.0",
               "user agent is sent as an HTTP header only");

        expect(request->body.find("\"clientName\":\"WEB\"") != std::string::npos,
               "minimal guest context includes client name");
        expect(request->body.find("\"clientVersion\":\"2.20260901.00.00\"") != std::string::npos,
               "minimal guest context includes client version");
        expect(request->body.find("\"hl\":\"en-CA\"") != std::string::npos,
               "minimal guest context includes response language");
        expect(request->body.find("\"gl\":\"CA\"") != std::string::npos,
               "minimal guest context includes region");
        expect(request->body.find("\"visitorData\":\"visitor-private-value\"") != std::string::npos,
               "minimal guest context includes required visitor data");
        expect(request->body.find("\"timeZone\":\"America/Toronto\"") != std::string::npos,
               "minimal guest context includes timezone shaping");
        expect(request->body.find("userAgent") == std::string::npos,
               "user agent is not redundantly copied into Search JSON");

        for (const auto forbidden : {
                 "clickTrackingParams", "adSignalsInfo", "deviceMake", "deviceModel",
                 "screenDensityFloat", "utcOffsetMinutes", "userInterfaceTheme",
                 "experimentsToken", "rolloutToken", "clientFormFactor",
             }) {
            expect(request->body.find(forbidden) == std::string::npos,
                   "official-client telemetry/bloat field is absent from Search request");
        }
    }

    const auto continuation = make_search_request(guest, "must-not-send", "OPAQUE-CONTINUATION");
    expect(continuation.has_value(), "bounded continuation request builds");
    if (continuation) {
        expect(continuation->body.find("OPAQUE-CONTINUATION") != std::string::npos,
               "continuation token is sent opaquely when needed");
        expect(continuation->body.find("must-not-send") == std::string::npos,
               "continuation request does not resend query");
    }

    expect(!make_search_request(guest, std::string(513, 'q')),
           "oversized Search query is rejected before request construction");
    expect(!make_search_request(guest, "q", std::string(64 * 1024 + 1, 'c')),
           "oversized continuation token is rejected before request construction");

    auto bad_version = guest;
    bad_version.api_version = "v1/../../evil";
    expect(!make_search_request(bad_version, "q"),
           "mutable API-version path injection is rejected");
    auto wrong_client = guest;
    wrong_client.client_name = "WEB_EMBEDDED_PLAYER";
    expect(!make_search_request(wrong_client, "q"),
           "unreviewed guest client identity is rejected");
    auto huge_visitor = guest;
    huge_visitor.visitor_data.assign(4097, 'v');
    expect(!make_search_request(huge_visitor, "q"),
           "oversized visitor/session identifier is rejected");

    expect(ttnx::net::safe_public_diagnostic(
               "sslConnectionDoHandshake failed: 0x12345678",
               "HTTPS failed") == "sslConnectionDoHandshake failed: 0x12345678",
           "safe technical diagnostic remains useful");
    expect(ttnx::net::safe_public_diagnostic(
               "Cookie: SID=secret",
               "HTTPS failed") == "HTTPS failed",
           "cookie-bearing diagnostic is replaced entirely");
    expect(ttnx::net::safe_public_diagnostic(
               "Authorization: Bearer secret",
               "HTTPS failed") == "HTTPS failed",
           "authorization-bearing diagnostic is replaced entirely");
    expect(ttnx::net::safe_public_diagnostic(
               "continuation token SECRET",
               "HTTPS failed") == "HTTPS failed",
           "continuation-bearing diagnostic is replaced entirely");
    const auto bounded_diag = ttnx::net::safe_public_diagnostic(std::string(500, 'x'), "fallback");
    expect(bounded_diag.size() <= 240, "public diagnostic length is bounded");

    SensitiveErrorClient sensitive_http;
    ttnx::core::GuestRequest search;
    search.surface = ttnx::core::BrowseSurface::Search;
    search.value = "private error test";
    const auto sensitive_failure = execute_guest_search(sensitive_http, guest, search);
    expect(!sensitive_failure.page.has_value(), "sensitive transport failure does not create a page");
    expect(sensitive_failure.error == "Guest Search HTTP request failed.",
           "executor never propagates visitor/continuation material from transport errors");
    expect(sensitive_failure.error.find("visitor-private-value") == std::string::npos,
           "visitor identifier cannot escape into executor diagnostic");
    expect(sensitive_failure.error.find("SECRET") == std::string::npos,
           "continuation token cannot escape into executor diagnostic");

    if (failures == 0) {
        std::cout << "All request contract and privacy tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
