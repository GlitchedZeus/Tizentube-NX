#include "tizentube_nx/playback/media_download.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace ttnx::playback;
namespace net = ttnx::net;

const std::string url = "https://r1.googlevideo.com/videoplayback?expire=1999999999";
int failures = 0;
void check(bool yes, const char* message) {
    if (!yes) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
struct FakeSink final : MediaChunkSink {
    std::string temporary;
    std::string published;
    int begins{0}, aborts{0}, finishes{0}, appends{0};
    bool begin_ok{true}, append_ok{true}, finish_ok{true};
    bool begin() override {
        ++begins;
        temporary.clear();
        return begin_ok;
    }
    bool append(std::string_view data) override {
        ++appends;
        if (!append_ok) return false;
        temporary.append(data);
        return true;
    }
    bool finish() override {
        ++finishes;
        if (!finish_ok) return false;
        published = std::move(temporary);
        temporary.clear();
        return true;
    }
    void abort() noexcept override { ++aborts; temporary.clear(); }
};
struct FakeTransport final : MediaChunkTransport {
    std::string payload;
    int calls{0};
    bool fail_network{false}, change_total{false};
    int transient_failures{0},transient_http_status{0};
    std::uint64_t advertized_total{0};
    net::HttpResult perform(const net::HttpRequest& req) override {
        ++calls;
        net::HttpResult r;
        if (transient_failures>0) {
            --transient_failures;
            r.response.status_code=transient_http_status;
            if(transient_http_status)r.ok=true;
            else r.error="network unavailable";
            return r;
        }
        if (fail_network) { r.error = "network unavailable"; return r; }
        std::uint64_t from = 0, to = 0;
        if (req.headers.empty() || req.headers[0].name != "Range" ||
            req.headers[0].value.rfind("bytes=",0) != 0) return r;
        const auto range = req.headers[0].value.substr(6);
        const auto dash = range.find('-');
        if (dash == std::string::npos) return r;
        from = std::stoull(range.substr(0,dash));
        to = std::stoull(range.substr(dash+1));
        const auto actual_size = static_cast<std::uint64_t>(payload.size());
        if (from >= actual_size) return r;
        const auto end = std::min(to, actual_size-1);
        const auto length = end - from + 1;
        r.ok = true;
        r.response.status_code = 206;
        r.response.body = payload.substr(static_cast<std::size_t>(from),
                                          static_cast<std::size_t>(length));
        const auto total = advertized_total ? advertized_total : actual_size;
        const auto reported_total = (change_total && calls == 1)
            ? total + 1 : total;
        r.response.headers = {
            {"Content-Type", "video/mp4"},
            {"Content-Length", std::to_string(length)},
            {"Content-Range", "bytes " + std::to_string(from) + "-" +
                              std::to_string(end) + "/" +
                              std::to_string(reported_total)},
        };
        return r;
    }
};

void short_success() {
    FakeTransport fake;
    fake.payload = "HELLO FROM SWITCH";
    FakeSink sink;
    MediaChunkReader reader(fake);
    MediaDownloadJob job(reader,sink);
    check(job.state() == MediaDownloadState::Idle, "initial idle");
    check(!job.start("https://accounts.nintendo.com/videoplayback"),
          "Nintendo source denied before sink created");
    check(sink.begins == 0 && fake.calls == 0, "invalid input no effects");
    check(job.start(url), "safe known source starts");
    check(job.state() == MediaDownloadState::Fetching &&
          fake.calls == 0, "start does no network work");
    check(job.pump() == MediaDownloadState::Complete, "tiny file completes in one bounded read");
    check(job.bytes_received() == fake.payload.size() &&
          job.total_bytes() == fake.payload.size() &&
          sink.published == fake.payload && sink.finishes == 1,
          "atomic success exactly matches source");
    check(job.pump() == MediaDownloadState::Complete && fake.calls == 1,
          "no duplicate network work after completion");
}
void large_success() {
    FakeTransport fake;
    fake.payload = std::string(MediaChunkReader::kMaxChunkBytes,'V') + "AAC";
    FakeSink sink;
    MediaChunkReader reader(fake);
    MediaDownloadJob job(reader,sink);
    check(job.start(url), "large download begins");
    check(job.pump() == MediaDownloadState::Fetching &&
          job.bytes_received() == MediaChunkReader::kMaxChunkBytes &&
          sink.published.empty(), "first 1MiB staged but not published");
    check(job.pump() == MediaDownloadState::Complete &&
          job.total_bytes() == fake.payload.size() &&
          sink.published == fake.payload && sink.finishes == 1 &&
          fake.calls == 2, "final partial chunk completes and publishes");
}
void negative_cases() {
    {
        FakeTransport fake;fake.payload="RECOVER";
        fake.transient_failures=1;fake.transient_http_status=503;
        FakeSink sink;MediaChunkReader reader(fake);
        MediaDownloadJob job(reader,sink);
        check(job.start(url),"bounded 503 test starts");
        check(job.pump()==MediaDownloadState::Fetching &&
              job.transient_attempts()==1 && sink.published.empty(),
              "transient 503 does not publish incomplete MP4");
        check(job.pump()==MediaDownloadState::Complete &&
              sink.published==fake.payload && fake.calls==2,
              "recovered signed 503 downloads and publishes once");
    }
    {
        FakeTransport fake;fake.payload="BLOCKED";
        fake.transient_failures=1;fake.transient_http_status=403;
        FakeSink sink;MediaChunkReader reader(fake);
        MediaDownloadJob job(reader,sink);
        check(job.start(url),"403 test starts");
        check(job.pump()==MediaDownloadState::Error &&
              fake.calls==1 && sink.aborts==1,
              "permanent signed 403 fails closed without retry");
    }
    {
        FakeTransport fake; fake.payload = std::string(MediaChunkReader::kMaxChunkBytes+9,'a');
        FakeSink sink; MediaChunkReader reader(fake); MediaDownloadJob job(reader,sink);
        check(job.start(url),"failure test starts");
        check(job.pump() == MediaDownloadState::Fetching,"first chunk downloaded");
        fake.fail_network=true;
        check(job.pump()==MediaDownloadState::Fetching &&
              job.transient_attempts()==1 &&
              job.retry_delay_ms()==250 && sink.aborts==0,
              "first missing connection retains staged bytes and backs off");
        check(job.pump()==MediaDownloadState::Fetching &&
              job.transient_attempts()==2 && job.retry_delay_ms()==500,
              "second transient connection failure bounded at 500ms");
        check(job.pump()==MediaDownloadState::Error &&
              job.error()==MediaDownloadError::RangeFailure &&
              sink.aborts==1 && sink.published.empty() &&
              sink.temporary.empty(),
              "third connection failure aborts before publishing partial cache");
    }
    {
        FakeTransport fake; fake.payload = std::string(MediaChunkReader::kMaxChunkBytes+9,'b');
        FakeSink sink; MediaChunkReader reader(fake); MediaDownloadJob job(reader,sink);
        fake.change_total=true; // first segment advertises size+1; second corrects it
        check(job.start(url),"change test starts");
        check(job.pump() == MediaDownloadState::Fetching,"first resource chunk accepted");
        check(job.pump() == MediaDownloadState::Error &&
              job.error() == MediaDownloadError::ResourceChanged &&
              sink.aborts == 1, "resource size change aborts");
    }
    {
        FakeTransport fake; fake.payload="HI";
        FakeSink sink; sink.append_ok=false;
        MediaChunkReader reader(fake); MediaDownloadJob job(reader,sink);
        check(job.start(url),"append error test starts");
        check(job.pump() == MediaDownloadState::Error &&
              job.error() == MediaDownloadError::SinkFailure &&
              sink.aborts == 1 && sink.published.empty(),
              "sink append failure aborts partial data");
    }
    {
        FakeTransport fake; fake.payload="HI";
        FakeSink sink; sink.finish_ok=false;
        MediaChunkReader reader(fake); MediaDownloadJob job(reader,sink);
        check(job.start(url),"finish error test starts");
        check(job.pump() == MediaDownloadState::Error &&
              job.error() == MediaDownloadError::SinkFailure &&
              sink.aborts == 1 && sink.published.empty(),
              "sink finalization failure aborts");
    }
    {
        FakeTransport fake; fake.payload=std::string(MediaChunkReader::kMaxChunkBytes,'z'); fake.advertized_total=MediaDownloadJob::kMaxFileBytes+1;
        FakeSink sink; MediaChunkReader reader(fake); MediaDownloadJob job(reader,sink);
        check(job.start(url),"too-large resource test starts");
        check(job.pump() == MediaDownloadState::Error &&
              job.error() == MediaDownloadError::ResourceTooLarge &&
              sink.aborts == 1 && sink.temporary.empty(),
              "unreasonably large total aborted before writing");
    }
    {
        FakeTransport fake; fake.payload=std::string(MediaChunkReader::kMaxChunkBytes+10,'x');
        FakeSink sink; MediaChunkReader reader(fake); MediaDownloadJob job(reader,sink);
        check(job.start(url),"cancel test starts");
        check(job.pump() == MediaDownloadState::Fetching,"first range staged");
        job.cancel();
        check(job.state() == MediaDownloadState::Cancelled &&
              sink.aborts == 1 && sink.temporary.empty() &&
              job.pump() == MediaDownloadState::Cancelled &&
              fake.calls == 1, "cancel clears sensitive temp data and stops network");
        check(job.start(url) && job.bytes_received() == 0 &&
              job.state() == MediaDownloadState::Fetching,
              "clean restart after cancellation");
    }
}
} // namespace
int main() {
    short_success();
    large_success();
    negative_cases();
    if (failures) std::cerr << failures << " media download tests failed\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
