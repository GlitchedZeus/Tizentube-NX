#include "tizentube_nx/youtube/guest_channel.hpp"

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
    session.visitor_data = "visitor-channel-test";
    session.language = "en";
    session.region = "CA";
    session.timezone = "America/Toronto";
    session.user_agent = "TizenTube-NX-Test";
    return session;
}

std::string response_fixture() {
    return R"JSON({
      "metadata": {"channelMetadataRenderer": {
        "title": "Guest Channel", "externalId": "UCabcdefghijklmnopqrstuv"
      }},
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": true, "content": {"richGridRenderer": {"contents": [
          {"richItemRenderer": {"content": {"videoRenderer": {
            "videoId": "abcdefghijk", "title": {"simpleText": "Safe Channel Video"}
          }}}}
        ]}}}}
      ]}}
    })JSON";
}

}  // namespace

int main() {
    using ttnx::core::BrowseSurface;
    using ttnx::core::GuestRequest;
    using ttnx::net::HttpMethod;
    using ttnx::youtube::execute_guest_channel;

    FakeHttpClient http;
    http.next_result.ok = true;
    http.next_result.response.status_code = 200;
    http.next_result.response.body = response_fixture();

    const auto session = usable_session();
    GuestRequest request;
    request.surface = BrowseSurface::Channel;
    request.value = "UCabcdefghijklmnopqrstuv";

    const auto result = execute_guest_channel(http, session, request);
    expect(result.page.has_value(), "guest Channel first page succeeds through fake transport");
    expect(http.calls == 1, "Channel performs exactly one explicit HTTP request");
    expect(http.last_request.method == HttpMethod::Post, "Channel uses POST");
    expect(http.last_request.url == "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false&alt=json",
           "Channel targets existing approved YouTube browse endpoint");
    expect(http.last_request.body.find("\"browseId\":\"UCabcdefghijklmnopqrstuv\"") != std::string::npos,
           "Channel request uses normalized browse ID");
    expect(http.last_request.body.find("clickTrackingParams") == std::string::npos,
           "Channel request omits tracking fields");
    expect(http.last_request.body.find("adSignalsInfo") == std::string::npos,
           "Channel request omits ad-signals fields");
    expect(result.diagnostics.find("visitor-channel-test") == std::string::npos,
           "Channel diagnostics never expose visitor data");

    FakeHttpClient wrong_http;
    GuestRequest wrong;
    wrong.surface = BrowseSurface::Search;
    wrong.value = request.value;
    expect(!execute_guest_channel(wrong_http, session, wrong).page.has_value(),
           "wrong surface rejected by Channel executor");
    expect(wrong_http.calls == 0, "wrong surface rejected before HTTP");

    GuestRequest continuation = request;
    continuation.continuation = "NOT-YET";
    expect(!execute_guest_channel(wrong_http, session, continuation).page.has_value(),
           "Channel continuation explicitly disabled for first-page hardware gate");
    expect(wrong_http.calls == 0, "disabled continuation rejected before HTTP");

    GuestRequest invalid_id = request;
    invalid_id.value = "bad";
    expect(!execute_guest_channel(wrong_http, session, invalid_id).page.has_value(),
           "invalid Channel ID rejected before HTTP");

    auto bad_session = session;
    bad_session.visitor_data.clear();
    expect(!execute_guest_channel(wrong_http, bad_session, request).page.has_value(),
           "unusable guest session rejected before HTTP");

    FakeHttpClient failed;
    failed.next_result.ok = false;
    failed.next_result.error = "X-Goog-Visitor-Id=visitor-channel-test continuation=SECRET";
    const auto transport = execute_guest_channel(failed, session, request);
    expect(!transport.page.has_value(), "Channel transport failure does not create a page");
    expect(transport.error == "Guest Channel HTTP request failed.",
           "Channel transport failure is privacy-sanitized");

    if (failures == 0) {
        std::cout << "All guest Channel executor tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
