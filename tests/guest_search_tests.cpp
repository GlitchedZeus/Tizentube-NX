#include "tizentube_nx/youtube/guest_search.hpp"

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
    session.client_version = "2.20260901.00.00";
    session.visitor_data = "visitor-test";
    session.language = "en";
    session.region = "CA";
    session.timezone = "America/Toronto";
    session.user_agent = "TizenTube-NX-Test";
    return session;
}

std::string response_fixture() {
    return R"JSON({
      "contents": {
        "twoColumnSearchResultsRenderer": {
          "primaryContents": {
            "sectionListRenderer": {
              "contents": [{
                "itemSectionRenderer": {
                  "contents": [
                    {"videoRenderer": {
                      "videoId": "safe-video",
                      "title": {"simpleText": "Safe Video"}
                    }},
                    {"videoRenderer": {
                      "videoId": "short-video",
                      "title": {"simpleText": "Never surface"},
                      "navigationEndpoint": {"reelWatchEndpoint": {"videoId": "short-video"}}
                    }},
                    {"promotedVideoRenderer": {
                      "videoRenderer": {
                        "videoId": "sponsored-video",
                        "title": {"simpleText": "Never surface either"}
                      }
                    }}
                  ]
                }
              }, {
                "continuationItemRenderer": {
                  "continuationEndpoint": {
                    "continuationCommand": {"token": "NEXT-TOKEN"}
                  }
                }
              }]
            }
          }
        }
      }
    })JSON";
}

}  // namespace

int main() {
    using ttnx::core::BrowseSurface;
    using ttnx::core::GuestRequest;
    using ttnx::net::HttpMethod;
    using ttnx::youtube::execute_guest_search;

    FakeHttpClient http;
    http.next_result.ok = true;
    http.next_result.response.status_code = 200;
    http.next_result.response.body = response_fixture();

    const auto session = usable_session();
    GuestRequest first_page;
    first_page.surface = BrowseSurface::Search;
    first_page.value = "switch homebrew";

    const auto first = execute_guest_search(http, session, first_page);
    expect(first.page.has_value(), "first-page guest Search succeeds through fake transport");
    expect(http.calls == 1, "first-page Search performs exactly one HTTP request");
    expect(http.last_request.method == HttpMethod::Post, "Search uses POST");
    expect(http.last_request.url == "https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json",
           "Search targets the exact YouTube InnerTube endpoint");
    expect(http.last_request.body.find("\"query\":\"switch homebrew\"") != std::string::npos,
           "first-page Search body contains query");
    expect(http.last_request.body.find("\"continuation\"") == std::string::npos,
           "first-page Search body omits continuation");
    if (first.page) {
        expect(first.page->items.size() == 1, "executor returns only sanitized visible Search items");
        expect(first.page->items[0].item.id == "safe-video", "safe video survives executor firewall");
        expect(first.page->continuation == "NEXT-TOKEN", "executor returns opaque continuation");
    }

    http.next_result.response.body = R"JSON({"onResponseReceivedCommands":[{"appendContinuationItemsAction":{"continuationItems":[{"videoRenderer":{"videoId":"page-two","title":{"simpleText":"Page Two"}}}]}}]})JSON";
    GuestRequest next_page;
    next_page.surface = BrowseSurface::Search;
    next_page.value = "switch homebrew";
    next_page.continuation = "NEXT-TOKEN";

    const auto next = execute_guest_search(http, session, next_page);
    expect(next.page.has_value(), "continuation Search succeeds through fake transport");
    expect(http.calls == 2, "continuation performs one additional HTTP request");
    expect(http.last_request.body.find("\"continuation\":\"NEXT-TOKEN\"") != std::string::npos,
           "continuation Search body carries opaque token");
    expect(http.last_request.body.find("\"query\"") == std::string::npos,
           "continuation Search does not resend original query");

    FakeHttpClient invalid_http;
    GuestRequest wrong_surface;
    wrong_surface.surface = BrowseSurface::Home;
    const auto wrong = execute_guest_search(invalid_http, session, wrong_surface);
    expect(!wrong.page.has_value(), "non-Search request rejected");
    expect(invalid_http.calls == 0, "invalid request is rejected before HTTP");

    auto bad_session = session;
    bad_session.visitor_data.clear();
    const auto missing_session = execute_guest_search(invalid_http, bad_session, first_page);
    expect(!missing_session.page.has_value(), "unusable guest session rejected");
    expect(invalid_http.calls == 0, "unusable session is rejected before HTTP");

    FakeHttpClient failed_http;
    failed_http.next_result.ok = false;
    failed_http.next_result.error = "simulated transport failure";
    const auto transport_failure = execute_guest_search(failed_http, session, first_page);
    expect(!transport_failure.page.has_value(), "transport failure does not create a page");
    expect(transport_failure.error == "simulated transport failure",
           "transport error propagated without inventing response data");

    FakeHttpClient malformed_http;
    malformed_http.next_result.ok = true;
    malformed_http.next_result.response.status_code = 200;
    malformed_http.next_result.response.body = "{broken";
    const auto malformed = execute_guest_search(malformed_http, session, first_page);
    expect(!malformed.page.has_value(), "malformed successful HTTP body rejected by parser");
    expect(!malformed.error.empty(), "malformed body returns parser error");

    if (failures == 0) {
        std::cout << "All guest Search executor tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
