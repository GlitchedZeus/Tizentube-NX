#include "tizentube_nx/playback/media_chunks.hpp"
#include "tizentube_nx/net/outbound_policy.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace ttnx::playback;
namespace net = ttnx::net;
int failures = 0;
void check(bool success, const char* name) {
    if (!success) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
const std::string kUrl =
    "https://r1---sn-example.googlevideo.com/videoplayback?expire=9999999999&sig=secret";

struct FakeMediaTransport final : MediaChunkTransport {
    int requests{0};
    net::HttpRequest last;
    net::HttpResult reply;
    net::HttpResult perform(const net::HttpRequest& request) override {
        ++requests;
        last = request;
        return reply;
    }
};

net::HttpResult response(std::string body, std::string content_range) {
    net::HttpResult r;
    r.ok = true;
    r.response.status_code = 206;
    r.response.body = std::move(body);
    r.response.headers = {
        {"Content-Type", "video/mp4"},
        {"Content-Length", std::to_string(r.response.body.size())},
        {"Content-Range", std::move(content_range)},
    };
    return r;
}

void basic() {
    FakeMediaTransport fake;
    MediaChunkReader chunks(fake);
    fake.reply = response("HELLO", "bytes 0-4/100");
    auto r = chunks.read(kUrl, {0, 4});
    check(r.ok() && r.bytes == "HELLO" && r.resource_size == 100,
          "valid 206 response with exact byte-range accepted");
    check(fake.requests == 1 && fake.last.method == net::HttpMethod::Get &&
          fake.last.url == kUrl && fake.last.body.empty(),
          "single GET with exact original signed URL");
    check(fake.last.headers.size() == 3 &&
          fake.last.headers[0].name == "Range" &&
          fake.last.headers[0].value == "bytes=0-4" &&
          fake.last.headers[1].value == "identity",
          "one explicit exact Range and no compression");

    fake.reply = response("WORLD", "bytes 1048576-1048580/9999999");
    r = chunks.read(kUrl, {1048576, 1048580});
    check(r.ok() && r.bytes == "WORLD" && r.resource_size == 9999999,
          "offset bytes and large video total accepted");
}

void fail_closed_destination_and_ranges() {
    FakeMediaTransport fake;
    MediaChunkReader chunks(fake);
    const std::vector<std::string> malicious = {
        "http://r1.googlevideo.com/videoplayback",
        "https://www.youtube.com/videoplayback",
        "https://accounts.nintendo.com/videoplayback",
        "https://r1.googlevideo.com.evil.test/videoplayback",
        "https://user@r1.googlevideo.com/videoplayback",
        "https://r1.googlevideo.com:1234/videoplayback",
        "https://r1.googlevideo.com/redirect",
        "https://r1.googlevideo.com/videoplayback#ignored",
        "https://r1.googlevideo.com/videoplayback\nHost: nintendo.com",
    };
    for (const auto& url : malicious) {
        auto r = chunks.read(url, {0, 4});
        check(!r.ok() && r.failure == MediaReadFailure::InvalidDestination,
              "invalid host/query/authority rejected before network call");
    }
    check(fake.requests == 0, "all invalid hosts rejected before backend");

    check(chunks.read(kUrl, {1, 0}).failure == MediaReadFailure::InvalidRange,
          "reverse range denied");
    check(chunks.read(kUrl, {0, 1048576}).failure == MediaReadFailure::InvalidRange,
          "range over 1MiB denied");
    check(chunks.read(kUrl, {0, 1024ULL * 1024ULL * 1024ULL * 1024ULL}).failure ==
          MediaReadFailure::InvalidRange, "range over max resource denied");
    check(fake.requests == 0, "invalid ranges never hit network");

    check(!net::is_allowed_outbound_host("r1.googlevideo.com"),
          "M2 network policy remains unchanged");
    check(!net::is_allowed_outbound_host("accounts.nintendo.com"),
          "Nintendo still hard-denied");
}

void response_contract() {
    FakeMediaTransport fake;
    MediaChunkReader chunks(fake);
    auto test = [&](net::HttpResult response, MediaReadFailure code, const char* name) {
        fake.reply = std::move(response);
        auto out = chunks.read(kUrl, {0, 4});
        check(!out.ok() && out.failure == code && out.bytes.empty(),
              name);
    };
    auto baseline = []() { return response("HELLO", "bytes 0-4/100"); };
    {
        auto x = baseline(); x.ok = false; x.error = "secret in upstream error";
        test(x, MediaReadFailure::TransportError, "network errors fail without URL leak");
    }
    {
        auto x = baseline(); x.response.status_code = 302;
        x.response.headers.push_back({"Location","https://accounts.nintendo.com"});
        test(x, MediaReadFailure::RedirectRejected, "redirect denied (even if response body looks like data)");
    }
    {
        auto x = baseline(); x.response.status_code = 200;
        test(x, MediaReadFailure::UnexpectedStatus, "unbounded 200 full download rejected");
    }
    {
        auto x = baseline(); x.response.status_code = 416;
        test(x, MediaReadFailure::UnexpectedStatus, "range failure does not become media data");
    }
    {
        auto x = baseline(); x.response.headers.push_back({"Location","https://other.test"});
        test(x, MediaReadFailure::InvalidPayloadHeaders, "unexpected Location on 206 denied");
    }
    {
        auto x = baseline(); x.response.headers.push_back({"Transfer-Encoding","chunked"});
        test(x, MediaReadFailure::InvalidPayloadHeaders, "chunked body forbidden");
    }
    {
        auto x = baseline(); x.response.headers.push_back({"Content-Encoding","gzip"});
        test(x, MediaReadFailure::InvalidPayloadHeaders, "gzip range forbidden");
    }
    {
        auto x = baseline(); x.response.headers.push_back({"Content-Type","text/html"});
        test(x, MediaReadFailure::InvalidPayloadHeaders, "conflicting content type denied");
    }
    {
        auto x = baseline(); x.response.headers[0].value="text/html";
        test(x, MediaReadFailure::InvalidPayloadHeaders, "HTML response denied");
    }
    {
        auto x = baseline(); x.response.headers[0].value="application/vnd.apple.mpegurl";
        test(x, MediaReadFailure::InvalidPayloadHeaders, "playlist manifest denied");
    }
    {
        auto x = baseline(); x.response.headers[1].value="6";
        test(x, MediaReadFailure::SizeMismatch, "body length mismatch denied");
    }
    {
        auto x = baseline(); x.response.body="ABCD";
        test(x, MediaReadFailure::SizeMismatch, "truncated body denied");
    }
    {
        auto x = baseline(); x.response.headers[2].value="bytes 1-5/100";
        test(x, MediaReadFailure::MissingOrInvalidRange, "wrong byte offset denied");
    }
    {
        auto x = baseline(); x.response.headers[2].value="bytes 0-4/*";
        test(x, MediaReadFailure::MissingOrInvalidRange, "unknown total denied");
    }
    {
        auto x = baseline(); x.response.headers[2].value="bytes 0-4/4";
        test(x, MediaReadFailure::MissingOrInvalidRange, "range beyond resource denied");
    }
    {
        auto x = baseline(); x.response.headers[2].value="bytes 0-4/18446744073709551616";
        test(x, MediaReadFailure::MissingOrInvalidRange, "resource size overflow denied");
    }
    {
        auto x = baseline(); x.response.headers[2].value="bytes 0-4/999999999999999";
        test(x, MediaReadFailure::MissingOrInvalidRange, "impossible resource size denied");
    }
    {
        auto x = baseline(); x.response.headers[0].value="video/mp4\r\nLocation: evil";
        test(x, MediaReadFailure::InvalidPayloadHeaders, "header injection denied");
    }
    {
        auto x = response("<html", "bytes 0-4/100"); x.response.headers[0].value="application/octet-stream";
        test(x, MediaReadFailure::MediaPayloadRejected, "HTML posing as media denied");
    }
    {
        auto x = response("HELLO","bytes 0-4/100");
        x.response.headers[0].value="audio/mp4";
        fake.reply = std::move(x);
        check(chunks.read(kUrl,{0,4}).ok(),"audio/mp4 byte-range permitted");
    }
}

void range_planner() {
    const auto first = next_media_range(0);
    check(first && first->first == 0 && first->last == 1048575,
          "1 MiB bounded first read");
    const auto second = next_media_range(1048576, 2000000);
    check(second && second->first == 1048576 && second->last == 1999999,
          "bounded final read at known resource end");
    check(!next_media_range(2000000, 2000000), "end of file has no range");
    check(!next_media_range(10, 0, 1048577), "unbounded chunk denied");
    check(!next_media_range(10, 0, 0), "empty chunk denied");
    check(!next_media_range(std::numeric_limits<std::uint64_t>::max()),
          "unbounded offset denied without integer overflow");
    auto last = next_media_range(1024ULL * 1024 * 1024 * 1024 - 1);
    check(last && last->first == last->last,
          "final legal byte does not overflow range");
}

} // namespace

int main() {
    basic();
    fail_closed_destination_and_ranges();
    response_contract();
    range_planner();
    if (failures) std::cerr << failures << " media policy failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
