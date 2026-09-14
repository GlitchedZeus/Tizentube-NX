#include "tizentube_nx/youtube/guest_search.hpp"

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

class FakeHttpClient final : public ttnx::net::HttpClient {
public:
    ttnx::net::HttpResult result;

    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest&) override {
        return result;
    }
};

ttnx::youtube::GuestSession session() {
    ttnx::youtube::GuestSession value;
    value.client_name = "WEB";
    value.client_version = "2.20260901.00.00";
    value.visitor_data = "visitor-test";
    value.language = "en";
    value.region = "CA";
    value.timezone = "America/Toronto";
    return value;
}

ttnx::core::GuestRequest request() {
    ttnx::core::GuestRequest value;
    value.surface = ttnx::core::BrowseSurface::Search;
    value.value = "test";
    return value;
}

}  // namespace

int main() {
    using ttnx::core::SearchErrorCode;
    using ttnx::youtube::execute_guest_search;

    FakeHttpClient policy;
    policy.result.ok = false;
    policy.result.error = "Outbound destination blocked by TizenTube NX network policy.";
    const auto policy_result = execute_guest_search(policy, session(), request());
    expect(!policy_result.page, "policy failure returns no page");
    expect(policy_result.error_code == SearchErrorCode::NetworkPolicyRejected,
           "policy failure gets policy classification");

    FakeHttpClient network;
    network.result.ok = false;
    network.result.error = "TCP connection to the approved YouTube host failed.";
    const auto network_result = execute_guest_search(network, session(), request());
    expect(network_result.error_code == SearchErrorCode::NetworkUnavailable,
           "generic transport failure gets network-unavailable classification");

    FakeHttpClient http;
    http.result.ok = true;
    http.result.response.status_code = 503;
    const auto http_result = execute_guest_search(http, session(), request());
    expect(http_result.error_code == SearchErrorCode::HttpFailure,
           "non-success HTTP status gets HTTP classification");

    FakeHttpClient malformed;
    malformed.result.ok = true;
    malformed.result.response.status_code = 200;
    malformed.result.response.body = "{broken";
    const auto malformed_result = execute_guest_search(malformed, session(), request());
    expect(malformed_result.error_code == SearchErrorCode::MalformedResponse,
           "malformed successful response gets malformed classification");

    FakeHttpClient unsupported;
    unsupported.result.ok = true;
    unsupported.result.response.status_code = 200;
    unsupported.result.response.body = R"JSON({"topbar":{"videoRenderer":{"videoId":"FAKE","title":{"simpleText":"fake"}}}})JSON";
    const auto unsupported_result = execute_guest_search(unsupported, session(), request());
    expect(unsupported_result.error_code == SearchErrorCode::UnsupportedResponse,
           "well-formed response without recognized Search payload gets unsupported classification");

    FakeHttpClient ok;
    ok.result.ok = true;
    ok.result.response.status_code = 200;
    ok.result.response.body = R"JSON({"contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{"sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[{"videoRenderer":{"videoId":"SAFEVIDEO01","title":{"simpleText":"Safe"}}}]}}]}}}}})JSON";
    const auto ok_result = execute_guest_search(ok, session(), request());
    expect(ok_result.page.has_value(), "valid Search still succeeds");
    expect(ok_result.error_code == SearchErrorCode::None,
           "successful Search has no error classification");
    expect(ok_result.error.empty(), "successful Search has no technical diagnostic");

    if (failures == 0) {
        std::cout << "All Search executor error tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
