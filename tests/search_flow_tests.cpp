#include "tizentube_nx/youtube/search_flow.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class ScriptedHttpClient final : public ttnx::net::HttpClient {
public:
    std::vector<ttnx::net::HttpResult> scripted;
    std::vector<ttnx::net::HttpRequest> requests;
    std::size_t index{0};

    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest& request) override {
        requests.push_back(request);
        if (index >= scripted.size()) {
            ttnx::net::HttpResult missing;
            missing.error = "no scripted response";
            return missing;
        }
        return scripted[index++];
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
    value.user_agent = "TizenTube-NX-Test";
    return value;
}

ttnx::net::HttpResult ok(std::string body) {
    ttnx::net::HttpResult result;
    result.ok = true;
    result.response.status_code = 200;
    result.response.body = std::move(body);
    return result;
}

ttnx::net::HttpResult failed(std::string message) {
    ttnx::net::HttpResult result;
    result.ok = false;
    result.error = std::move(message);
    return result;
}

std::string first_page(std::string id, std::string token = {}) {
    std::string continuation;
    if (!token.empty()) {
        continuation = ", {\"continuationItemRenderer\":{\"continuationEndpoint\":{"
            "\"continuationCommand\":{\"token\":\"" + token + "\"}}}}";
    }
    return "{\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContents\":{"
        "\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":["
        "{\"videoRenderer\":{\"videoId\":\"" + id +
        "\",\"title\":{\"simpleText\":\"Result\"}}}}]}" + continuation + "]}}}}}";
}

std::string empty_first_page(std::string token) {
    return "{\"contents\":{\"twoColumnSearchResultsRenderer\":{\"primaryContents\":{"
        "\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":[]}},"
        "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{"
        "\"token\":\"" + token + "\"}}}}]}}}}}";
}

std::string continuation_page(std::string id, std::string token = {}) {
    std::string tail;
    if (!token.empty()) {
        tail = ", {\"continuationItemView\":{\"continuationCommand\":{\"token\":\"" +
               token + "\"}}}";
    }
    return "{\"onResponseReceivedCommands\":[{\"appendContinuationItemsAction\":{"
        "\"continuationItems\":[{\"videoRenderer\":{\"videoId\":\"" + id +
        "\",\"title\":{\"simpleText\":\"Next\"}}}" + tail + "]}}]}";
}

}  // namespace

int main() {
    using ttnx::youtube::GuestSearchFlow;

    const auto guest = session();

    ScriptedHttpClient happy;
    happy.scripted = {
        ok(first_page("FIRSTPAGE01", "TOKEN-1")),
        ok(continuation_page("SECONDPAGE1", "TOKEN-2")),
        ok(continuation_page("THIRDPAGE01", "TOKEN-2")),
    };
    GuestSearchFlow flow;
    const auto first = flow.begin(happy, guest, "query A");
    expect(first.page.has_value(), "first request succeeds");
    expect(flow.query() == "query A", "flow records the active query only after success");
    expect(flow.next_continuation() == "TOKEN-1", "first response continuation becomes current token");
    expect(happy.requests.size() == 1, "first page performs one request");
    if (!happy.requests.empty()) {
        expect(happy.requests[0].body.find("\"query\":\"query A\"") != std::string::npos,
               "first request contains query");
        expect(happy.requests[0].body.find("\"continuation\"") == std::string::npos,
               "first request contains no continuation");
    }

    const auto second = flow.next(happy, guest);
    expect(second.page.has_value(), "successful continuation succeeds");
    expect(flow.next_continuation() == "TOKEN-2", "successful continuation advances token");
    expect(happy.requests.size() == 2, "continuation performs exactly one request");
    if (happy.requests.size() >= 2) {
        expect(happy.requests[1].body.find("\"continuation\":\"TOKEN-1\"") != std::string::npos,
               "continuation request carries only the owned token");
        expect(happy.requests[1].body.find("\"query\"") == std::string::npos,
               "continuation request does not resend query");
    }

    const auto repeated = flow.next(happy, guest);
    expect(repeated.page.has_value(), "page with repeated next token remains usable");
    expect(!flow.can_continue(), "repeated continuation token terminates pagination loop");
    const auto no_more = flow.next(happy, guest);
    expect(!no_more.page.has_value(), "next is rejected after repeated token ends pagination");
    expect(happy.requests.size() == 3, "no-token next fails before HTTP");

    ScriptedHttpClient empty_then_next;
    empty_then_next.scripted = {
        ok(empty_first_page("EMPTY-NEXT")),
        ok(continuation_page("AFTEREMPTY1")),
    };
    GuestSearchFlow empty_flow;
    const auto empty = empty_flow.begin(empty_then_next, guest, "empty first");
    expect(empty.page && empty.page->results.empty(), "empty first page can still succeed");
    expect(empty_flow.can_continue(), "empty first page may legitimately carry a continuation");
    const auto after_empty = empty_flow.next(empty_then_next, guest);
    expect(after_empty.page && !after_empty.page->results.empty(),
           "continuation after empty first page succeeds");
    expect(!empty_flow.can_continue(), "missing next token ends pagination cleanly");

    ScriptedHttpClient ambiguous;
    ambiguous.scripted = {ok(R"JSON({
      "contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{
        "sectionListRenderer":{"contents":[
          {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"ONE"}}}},
          {"continuationItemViewModel":{"continuationCommand":{"token":"TWO"}}}
        ]}
      }}}
    })JSON")};
    GuestSearchFlow ambiguous_flow;
    const auto ambiguous_page = ambiguous_flow.begin(ambiguous, guest, "ambiguous");
    expect(ambiguous_page.page.has_value(), "duplicate distinct continuation candidates keep page usable");
    expect(!ambiguous_flow.can_continue(), "ambiguous continuation candidates disable pagination");

    ScriptedHttpClient retry;
    retry.scripted = {
        ok(first_page("RETRYFIRST1", "RETRY-TOKEN")),
        failed("simulated failure"),
        ok(continuation_page("RETRYSUCC01")),
    };
    GuestSearchFlow retry_flow;
    expect(retry_flow.begin(retry, guest, "retry query").page.has_value(), "retry fixture first page succeeds");
    const auto transport_fail = retry_flow.next(retry, guest);
    expect(!transport_fail.page.has_value(), "HTTP failure does not create continuation page");
    expect(retry_flow.next_continuation() == "RETRY-TOKEN",
           "HTTP failure does not consume or advance continuation state");
    const auto retry_ok = retry_flow.next(retry, guest);
    expect(retry_ok.page.has_value(), "same continuation can be deliberately retried after HTTP failure");

    ScriptedHttpClient parse_retry;
    parse_retry.scripted = {
        ok(first_page("PARSEFIRST1", "PARSE-TOKEN")),
        ok("{broken"),
        ok(continuation_page("PARSEGOOD01")),
    };
    GuestSearchFlow parse_flow;
    expect(parse_flow.begin(parse_retry, guest, "parse retry").page.has_value(), "parse fixture first page succeeds");
    const auto parse_fail = parse_flow.next(parse_retry, guest);
    expect(!parse_fail.page.has_value(), "malformed continuation response is rejected");
    expect(parse_flow.next_continuation() == "PARSE-TOKEN",
           "parse failure leaves continuation state retryable");
    expect(parse_flow.next(parse_retry, guest).page.has_value(),
           "continuation can be retried after parse failure");

    ScriptedHttpClient isolate;
    isolate.scripted = {
        ok(first_page("QUERYA00001", "TOKEN-A")),
        ok(first_page("QUERYB00001")),
    };
    GuestSearchFlow isolate_flow;
    expect(isolate_flow.begin(isolate, guest, "query A").page.has_value(), "query A succeeds");
    expect(isolate_flow.next_continuation() == "TOKEN-A", "query A owns token A");
    expect(isolate_flow.begin(isolate, guest, "query B").page.has_value(), "brand-new query B succeeds");
    expect(isolate_flow.query() == "query B", "new query replaces old query state");
    expect(!isolate_flow.can_continue(), "query A continuation is cleared when query B starts");
    const auto stale_next = isolate_flow.next(isolate, guest);
    expect(!stale_next.page.has_value(), "old query continuation cannot be requested after new query begins");
    expect(isolate.requests.size() == 2, "stale continuation is blocked before HTTP");

    ScriptedHttpClient failed_begin;
    failed_begin.scripted = {failed("first request failed")};
    GuestSearchFlow failed_begin_flow;
    const auto begin_failure = failed_begin_flow.begin(failed_begin, guest, "will fail");
    expect(!begin_failure.page.has_value(), "failed initial query propagates failure");
    expect(!failed_begin_flow.active(), "failed initial query leaves no stale active state");

    if (failures == 0) {
        std::cout << "All guest Search flow state tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
