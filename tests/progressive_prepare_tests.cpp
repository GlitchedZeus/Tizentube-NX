#include "tizentube_nx/playback/progressive_prepare.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

using namespace ttnx::playback;
namespace {
int fails=0;
const std::string kId="IzJ7R4EnYmI";
const std::string kUrl="https://r1.googlevideo.com/videoplayback?expire=1999999999";
void check(bool x,const char* s) {if(!x){++fails;std::cerr<<"FAIL "<<s<<"\n";}}

struct FakeExtractor final : Extractor {
    int calls{0};
    bool offline{false};
    ExtractionResult value;
    FakeExtractor() {
        value.video_id=kId;value.duration_ms=30000;
        value.video.push_back({kUrl,VideoCodec::H264,AudioCodec::Aac,
            640,360,30,8,500000,false,false});
        // A higher-resolution stream with external audio must not be chosen
        // for a local MP4 until the player supports adaptive merging.
        value.video.push_back({kUrl,VideoCodec::H264,AudioCodec::Unknown,
            1280,720,30,8,1800000,false,false});
        value.audio.push_back({kUrl,AudioCodec::Aac,128000,false});
    }
    std::optional<ExtractionResult> resolve(std::string_view id) override {
        ++calls;
        if (offline || id!=kId) return std::nullopt;
        return value;
    }
};
struct FakeTransport final : MediaChunkTransport {
    std::string body="NATIVE-MEDIA-CACHE";
    int calls{0};
    bool fail{false};
    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest& req) override {
        ++calls;
        ttnx::net::HttpResult out;
        if(fail) return out;
        const auto range=req.headers[0].value.substr(6);
        const auto dash=range.find('-');
        const auto first=std::stoull(range.substr(0,dash));
        const auto last=std::stoull(range.substr(dash+1));
        if(first>=body.size())return out;
        const auto final=std::min<std::uint64_t>(last,body.size()-1);
        out.ok=true;
        out.response.status_code=206;
        out.response.body=body.substr(static_cast<std::size_t>(first),
                                      static_cast<std::size_t>(final-first+1));
        out.response.headers={
            {"Content-Type","video/mp4"},
            {"Content-Range","bytes "+std::to_string(first)+"-"+
                std::to_string(final)+"/"+std::to_string(body.size())},
            {"Content-Length",std::to_string(out.response.body.size())}
        };
        return out;
    }
};
struct FakeSink final : MediaChunkSink {
    std::string partial,published;
    int starts{0},aborts{0},finishes{0};
    bool begin() override {++starts;partial.clear();return true;}
    bool append(std::string_view b) override {partial.append(b);return true;}
    bool finish() override {++finishes;published=std::move(partial);return true;}
    void abort() noexcept override {++aborts;partial.clear();}
};
void success() {
    FakeExtractor resolver;
    FakeTransport transport;
    FakeSink sink;
    MediaChunkReader reader(transport);
    MediaDownloadJob cache(reader,sink);
    ProgressivePrepareJob job(resolver,cache);
    check(job.start(kId),"valid normal video starts native preparation");
    check(job.stage()==ProgressiveStage::Downloading &&
          transport.calls==0 && resolver.calls==1 &&
          sink.published.empty(),"source resolution does not start media transport on UI thread");
    check(job.pump()==ProgressiveStage::Ready && transport.calls==1 &&
          sink.published==transport.body && sink.finishes==1,
          "real-video muxed H264/AAC downloaded and atomically published");
    check(job.video_id()==kId && job.duration_ms()==30000 &&
          job.received_bytes()==transport.body.size(),
          "source video identity/duration and transfer progress persisted");
    check(job.pump()==ProgressiveStage::Ready && transport.calls==1,
          "repeated pump never re-downloads");
}
void rejects() {
    FakeExtractor resolver;
    FakeTransport transport;
    FakeSink sink;
    MediaChunkReader reader(transport);
    MediaDownloadJob cache(reader,sink);
    ProgressivePrepareJob job(resolver,cache);
    check(!job.start("/shorts/IzJ7R4EnYmI") &&
          job.error()==ProgressiveError::InvalidId &&
          resolver.calls==0, "Shorts URL forbidden before resolver or media transport");
    resolver.offline=true;
    check(!job.start(kId) && job.error()==ProgressiveError::ResolverUnavailable,
          "unavailable YouTube source explicitly reported");
    resolver.offline=false;
    resolver.value.video_id="OtherId111";
    check(!job.start(kId) && job.error()==ProgressiveError::IdentityMismatch,
          "source video identity change denied before transfer");
    resolver.value.video_id=kId;
    resolver.value.is_short=true;
    check(!job.start(kId) && job.error()==ProgressiveError::ShortsForbidden,
          "upstream Shorts classification blocks all downloads");
    resolver.value.is_short=false;
    resolver.value.video.erase(resolver.value.video.begin());
    check(!job.start(kId) && job.error()==ProgressiveError::NoSafeMuxedMp4,
          "adaptive-only stream cannot silently lose audio");
    resolver.value.video[0].muxed_audio=AudioCodec::Aac;
    resolver.value.video[0].url="https://accounts.nintendo.com/videoplayback";
    check(!job.start(kId) && job.error()==ProgressiveError::NoSafeMuxedMp4,
          "unsafe resolved media host never reaches socket");
    check(transport.calls==0 && sink.published.empty(),
          "all invalid sources blocked before network fetch");
}
void cancellation() {
    FakeExtractor resolver;
    FakeTransport transport;
    transport.body=std::string(MediaChunkReader::kMaxChunkBytes+4,'X');
    FakeSink sink;
    MediaChunkReader reader(transport);
    MediaDownloadJob cache(reader,sink);
    ProgressivePrepareJob job(resolver,cache);
    check(job.start(kId),"multi-range source starts");
    check(job.pump()==ProgressiveStage::Downloading && sink.published.empty(),
          "partial cache cannot reach media player");
    job.cancel();
    check(job.stage()==ProgressiveStage::Cancelled &&
          sink.partial.empty() && sink.published.empty() &&
          job.video_id().empty(),"cancel discards incomplete temp media and source identity");
    check(job.pump()==ProgressiveStage::Cancelled && transport.calls==1,
          "cancelled job does not resume socket read");
    check(job.start(kId),"can start fresh source after cancellation");
    transport.fail=true;
    check(job.pump()==ProgressiveStage::Downloading &&
          cache.transient_attempts()==1 && cache.retry_delay_ms()==250 &&
          sink.published.empty(),
          "first temporary failure retains private partial media for bounded retry");
    check(job.pump()==ProgressiveStage::Downloading &&
          cache.transient_attempts()==2 && cache.retry_delay_ms()==500 &&
          sink.published.empty(),
          "second temporary failure remains bounded and unpublished");
    check(job.pump()==ProgressiveStage::Error &&
          job.error()==ProgressiveError::DownloadFailed &&
          sink.published.empty() && sink.partial.empty() &&
          transport.calls==5,
          "third persistent network failure aborts and erases partial media");
}
} // namespace
int main() {
    success();rejects();cancellation();
    if(fails)std::cerr<<fails<<" progressive integration failures\n";
    return fails?EXIT_FAILURE:EXIT_SUCCESS;
}
