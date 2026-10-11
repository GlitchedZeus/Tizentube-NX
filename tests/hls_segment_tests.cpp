#include "tizentube_nx/playback/hls_segment.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace ttnx;
namespace {
int fails=0;
void check(bool ok,const char* msg) {
    if(!ok){++fails;std::cerr<<"FAIL "<<msg<<"\n";}
}
constexpr const char* kUrl="https://r1.googlevideo.com/videoplayback?expire=123&sq=1";
struct Fake:net::HttpClient {
    int calls{0};
    net::HttpRequest request;
    net::HttpResult result;
    Fake() {
        result.ok=true;
        result.response.status_code=200;
        result.response.body=std::string("\0\0\0\x18ftypisom0000",16);
        result.response.headers={
            {"Content-Type","video/mp4"},
            {"Content-Length",std::to_string(result.response.body.size())}
        };
    }
    net::HttpResult perform(const net::HttpRequest& r) override {
        ++calls;request=r;
        return result;
    }
};
void tests() {
    Fake fake;
    playback::HlsSegmentReader reader(fake);
    auto r=reader.fetch(kUrl);
    check(r.ok() && r.bytes.size()==16 &&
          fake.request.method==net::HttpMethod::Get &&
          fake.request.headers.size()==2,
          "reviewed segment GET returns exact binary bytes");
    check(!reader.fetch("https://accounts.nintendo.com/videoplayback").ok() &&
          fake.calls==1,"Nintendo blocked before network");
    check(!reader.fetch("https://r1.googlevideo.com.evil.example/videoplayback").ok() &&
          fake.calls==1,"spoofed suffix blocked before network");
    fake.result.response.status_code=302;
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::HttpStatus,
          "redirect blocked without following");
    fake.result.response.status_code=200;
    fake.result.response.headers={{"Content-Type","text/html"}};
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::WrongType,
          "HTML response MIME never treated as media");
    fake.result.response.headers={{"Content-Type","video/mp4"},
                                  {"Content-Encoding","gzip"}};
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::CompressedOrAmbiguous,
          "compressed media response rejected");
    fake.result.response.headers={{"Content-Type","video/mp4"},
                                  {"Content-Length","999"}};
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::InvalidLength,
          "wrong content length rejected");
    fake.result.response.headers={{"Content-Type","video/mp4"},
                                  {"Content-Type","video/mp4"}};
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::CompressedOrAmbiguous,
          "duplicate MIME headers denied");
    fake.result.response.headers={{"Content-Type","video/mp4"}};
    fake.result.response.body="<html>oops rejected upstream</html>";
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::NotMedia,
          "HTML body refused even when server lies about MIME");
    fake.result.response.body=std::string(playback::HlsSegmentReader::kMaximumSegmentBytes+1,'X');
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::InvalidLength,
          "oversized 16MiB segment refused before being published");
    fake.result.ok=false;fake.result.response.body=std::string("error");
    r=reader.fetch(kUrl);
    check(!r.ok() && r.failure==playback::HlsSegmentFailure::Transport,
          "socket failure returns no data");
}
} // namespace
int main() {
    tests();
    if(fails)std::cerr<<fails<<" HLS segment failures\n";
    return fails?EXIT_FAILURE:EXIT_SUCCESS;
}
