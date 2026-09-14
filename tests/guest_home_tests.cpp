#include "tizentube_nx/youtube/guest_home.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
    int calls{0};
    ttnx::net::HttpRequest last_request;
    ttnx::net::HttpResult next_result;

    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest& request) override {
        ++calls;
        last_request = request;
        return next_result;
    }
};

ttnx::youtube::GuestSession usable_session() {
    ttnx::youtube::GuestSession session;
    session.client_name = "WEB";
    session.client_name_id = "1";
    session.client_version = "2.20260901.00.00";
    session.visitor_data = "visitor-home-test";
    session.language = "en";
    session.region = "CA";
    session.timezone = "America/Toronto";
    session.user_agent = "TizenTube-NX-Test";
    return session;
}

std::string response_fixture() {
    return R"JSON({
      "topbar": {"videoRenderer": {
        "videoId": "hostile-topbar",
        "title": {"simpleText": "Out of scope"}
      }},
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {
          "selected": true,
          "content": {"richGridRenderer": {"contents": [
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "safe-home-video",
              "title": {"simpleText": "Safe Home Video"}
            }}}},
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "home-short",
              "title": {"simpleText": "Never"},
              "navigationEndpoint": {"reelWatchEndpoint": {"videoId": "home-short"}}
            }}}}
          ]}}
        }}
      ]}}
    })JSON";
}

bool contains_id(const ttnx::core::HomePage& page, const std::string& id) {
    for (const auto& result : page.results) {
        if (result.id == id) return true;
    }
    return false;
}

}  // namespace

int main() {
    using ttnx::core::BrowseSurface;
    using ttnx::core::GuestRequest;
    using ttnx::net::HttpMethod;
    using ttnx::youtube::execute_guest_home;

    FakeHttpClient http;
    http.next_result.ok = true;
    http.next_result.response.status_code = 200;
    http.next_result.response.body = response_fixture();

    const auto session = usable_session();
    GuestRequest home;
    home.surface = BrowseSurface::Home;

    const auto result = execute_guest_home(http, session, home);
    expect(result.page.has_value(), "guest Home first page succeeds through fake transport");
    expect(!result.diagnostics.empty(), "guest Home propagates structural diagnostics");
    expect(result.diagnostics.find("richGridRenderer:") != std::string::npos,
           "guest Home diagnostics retain renderer-family structure");
    expect(result.diagnostics.find("safe-home-video") == std::string::npos,
           "guest Home diagnostics never expose video IDs");
    expect(result.diagnostics.find("visitor-home-test") == std::string::npos,
           "guest Home diagnostics never expose visitor data");
    expect(http.calls == 1, "guest Home performs exactly one HTTP request");
    expect(http.last_request.method == HttpMethod::Post, "Home uses POST");
    expect(http.last_request.url == "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false&alt=json",
           "Home targets exact approved YouTube browse endpoint");
    expect(http.last_request.body.find("\"browseId\":\"FEwhat_to_watch\"") != std::string::npos,
           "Home first-page request uses FEwhat_to_watch browse id");
    expect(http.last_request.body.find("\"query\"") == std::string::npos,
           "Home request never carries Search query state");
    expect(http.last_request.body.find("clickTrackingParams") == std::string::npos,
           "Home request omits tracking fields");
    expect(http.last_request.body.find("adSignalsInfo") == std::string::npos,
           "Home request omits ad-signals fields");
    if (result.page) {
        expect(result.page->results.size() == 1, "Home executor returns sanitized visible results only");
        expect(contains_id(*result.page, "safe-home-video"), "normal Home video survives executor");
        expect(!contains_id(*result.page, "home-short"), "Home executor blocks disguised Short");
        expect(!contains_id(*result.page, "hostile-topbar"),
               "Home executor keeps unrelated renderer-shaped topbar out of scope");
    }

    FakeHttpClient invalid_http;
    GuestRequest wrong_surface;
    wrong_surface.surface = BrowseSurface::Search;
    wrong_surface.value = "not Home";
    const auto wrong = execute_guest_home(invalid_http, session, wrong_surface);
    expect(!wrong.page.has_value(), "non-Home request rejected by Home executor");
    expect(invalid_http.calls == 0, "wrong surface rejected before HTTP");

    auto bad_session = session;
    bad_session.visitor_data.clear();
    const auto missing_session = execute_guest_home(invalid_http, bad_session, home);
    expect(!missing_session.page.has_value(), "unusable guest session rejected");
    expect(invalid_http.calls == 0, "unusable guest session rejected before HTTP");

    FakeHttpClient failed_http;
    failed_http.next_result.ok = false;
    failed_http.next_result.error =
        "transport failed; X-Goog-Visitor-Id=visitor-home-test; continuation=SECRET";
    const auto transport_failure = execute_guest_home(failed_http, session, home);
    expect(!transport_failure.page.has_value(), "Home transport failure does not create a page");
    expect(transport_failure.error == "Guest Home HTTP request failed.",
           "Home executor redacts visitor/continuation material from transport failure");

    FakeHttpClient http_error;
    http_error.next_result.ok = true;
    http_error.next_result.response.status_code = 503;
    const auto non_success = execute_guest_home(http_error, session, home);
    expect(!non_success.page.has_value(), "non-success Home HTTP status rejected");

    FakeHttpClient malformed;
    malformed.next_result.ok = true;
    malformed.next_result.response.status_code = 200;
    malformed.next_result.response.body = "{broken";
    const auto malformed_result = execute_guest_home(malformed, session, home);
    expect(!malformed_result.page.has_value(), "malformed Home body rejected");
    expect(!malformed_result.error.empty(), "malformed Home body returns safe diagnostic");

    if (failures == 0) {
        std::cout << "All guest Home executor tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
