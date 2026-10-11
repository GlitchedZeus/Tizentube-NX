#include "tizentube_nx/playback/hls_cache.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ttnx;
namespace {
int failures=0;
void check(bool result,const char* desc) {
    if(!result){++failures;std::cerr<<"FAIL "<<desc<<"\n";}
}
const std::string base="https://r1.googlevideo.com/videoplayback?";
playback::HlsProbePlan plan() {
    playback::HlsProbePlan p;
    p.source.video_id="2kD9v7Q3vec";
    p.source.duration_ms=12000;
    p.selected.height=720;p.selected.fps=30;
    p.selected.playlist_url=
        "https://manifest.googlevideo.com/api/manifest/hls_variant/v720";
    p.selected.audio_playlist_url=
        "https://manifest.googlevideo.com/api/manifest/hls_variant/audio";
    p.video_segments.init_segment_url=base+"initv";
    p.video_segments.segments={{base+"v1",6000},{base+"v2",6000}};
    p.video_segments.total_duration_ms=12000;
    playback::HlsMediaPlaylist audio;
    audio.init_segment_url=base+"inita";
    audio.segments={{base+"a1",6000},{base+"a2",6000}};
    audio.total_duration_ms=12000;
    p.audio_segments=audio;
    return p;
}
struct FakeTransport:net::HttpClient {
    int hits{0};
    int fail_after{0};
    net::HttpResult perform(const net::HttpRequest& request) override {
        ++hits;
        net::HttpResult out;
        out.ok=fail_after==0 || hits!=fail_after;
        out.response.status_code=out.ok?200:403;
        out.response.body=std::string("\0\0\0\x18ftypisom0000",16);
        out.response.headers={{"Content-Type","video/mp4"},
                              {"Content-Length","16"}};
        if(request.url.find("googlevideo.com/")==std::string::npos)
            out.ok=false;
        return out;
    }
};
struct FakeAacTransport:playback::MediaChunkTransport {
    int requests{0};
    bool blocked{false};
    net::HttpResult perform(const net::HttpRequest& req) override {
        ++requests;
        net::HttpResult out;
        out.ok=!blocked && req.method==net::HttpMethod::Get &&
            req.headers.size()==3 &&
            req.headers[0].value=="bytes=0-1048575";
        out.response.status_code=out.ok?206:403;
        out.response.body=std::string(2048,'A');
        out.response.body.replace(0,12,std::string("\0\0\0\x18ftypisom",12));
        out.response.headers={
            {"Content-Type","audio/mp4"},
            {"Content-Length","2048"},
            {"Content-Range","bytes 0-2047/2048"}
        };
        return out;
    }
};

struct Sink:playback::HlsCacheSink {
    int begins{0},writes{0},publishes{0},aborts{0};
    int audio_appends{0};
    std::size_t aac_bytes{0};
    bool reject_write{false},reject_publish{false};
    bool completed{false};
    std::vector<std::string> written;
    bool begin(const playback::HlsProbePlan&) override {
        ++begins;written.clear();completed=false;return true;
    }
    bool write_segment(bool audio,bool init,std::size_t i,std::string_view data) override {
        ++writes;
        if(reject_write)return false;
        written.push_back(std::string(audio?"audio":"video")+
              (init?"-init":"-segment"+std::to_string(i))+":"+
              std::to_string(data.size()));
        return true;
    }
    bool append_external_aac(std::string_view data) override {
        ++audio_appends;
        aac_bytes+=data.size();
        return !reject_write;
    }
    bool publish(const playback::HlsProbePlan&) override {
        ++publishes;
        if(reject_publish)return false;
        completed=true;return true;
    }
    void abort() noexcept override {
        ++aborts;written.clear();completed=false;
    }
};
void full() {
    FakeTransport transport;
    playback::HlsSegmentReader reader(transport);
    Sink sink;
    playback::HlsCacheJob job(reader,sink);
    check(job.start(plan()),"validated full A/V HLS clip starts cache staging");
    check(job.state()==playback::HlsCacheState::Fetching &&
          transport.hits==0 && sink.publishes==0,
          "start stages but never makes network calls on UI thread");
    for(int i=0;i<10 && job.state()==playback::HlsCacheState::Fetching;++i)
        (void)job.pump();
    check(job.state()==playback::HlsCacheState::Ready &&
          transport.hits==6 && sink.publishes==1 &&
          sink.written.size()==6 && sink.completed,
          "video+audio init and four independent segments published exactly once");
    check(job.downloaded_bytes()==96 && job.segments_completed()==4,
          "bounded progress counts all bytes but only proper media segments");
    (void)job.pump();
    check(sink.publishes==1,"completed video cache cannot publish twice");
}
void failure_cases() {
    {
        FakeTransport transport;transport.fail_after=2;
        playback::HlsSegmentReader reader(transport);
        Sink sink;playback::HlsCacheJob job(reader,sink);
        check(job.start(plan()),"staging starts");
        (void)job.pump();
        check(job.pump()==playback::HlsCacheState::Failed &&
              job.error()==playback::HlsCacheFailure::MediaFetch &&
              sink.aborts>=1 && !sink.completed && sink.publishes==0,
              "403 mid-clip aborts all cache and never opens mpv");
    }
    {
        FakeTransport transport;
        playback::HlsSegmentReader reader(transport);
        Sink sink;playback::HlsCacheJob job(reader,sink);
        check(job.start(plan()),"cancellable run begins");
        (void)job.pump();
        job.cancel();
        check(job.state()==playback::HlsCacheState::Cancelled &&
              !sink.completed && sink.written.empty(),
              "cancellation destroys incomplete clip cache");
        (void)job.pump();
        check(transport.hits==1,"cancelled cache never continues network requests");
    }
    {
        FakeTransport transport;
        playback::HlsSegmentReader reader(transport);
        Sink sink;sink.reject_publish=true;
        playback::HlsCacheJob job(reader,sink);
        check(job.start(plan()),"publish failure fixture starts");
        for(int i=0;i<10 && job.state()==playback::HlsCacheState::Fetching;++i)
            (void)job.pump();
        check(job.state()==playback::HlsCacheState::Failed &&
              job.error()==playback::HlsCacheFailure::UnableToPublish &&
              !sink.completed,
              "partial clip not visible if atomic manifest publishing fails");
    }
    {
        FakeTransport transport;
        playback::HlsSegmentReader reader(transport);
        Sink sink;playback::HlsCacheJob job(reader,sink);
        auto source=plan();
        source.audio_segments.reset();
        source.selected.audio_playlist_url.clear();
        source.selected.direct_media_playlist=true;
        source.selected.height=0;
        source.selected.fps=0;
        check(job.start(source),"direct HLS source can stage without invented resolution");
        for(int i=0;i<8 && job.state()==playback::HlsCacheState::Fetching;++i)
            (void)job.pump();
        check(job.state()==playback::HlsCacheState::Failed &&
              job.error()==playback::HlsCacheFailure::MissingAudio &&
              sink.publishes==0 && !sink.completed,
              "direct video-only stream must never publish silent playback");
    }
    {
        FakeTransport video;
        FakeAacTransport audio;
        playback::HlsSegmentReader video_reader(video);
        playback::MediaChunkReader audio_reader(audio);
        Sink sink;
        playback::HlsCacheJob job(video_reader,sink,&audio_reader);
        auto p=plan();
        p.audio_segments.reset();
        p.selected.audio_playlist_url.clear();
        p.selected.direct_media_playlist=true;
        p.selected.height=0;p.selected.fps=0;
        p.source.external_aac_url=
            "https://r2.googlevideo.com/videoplayback?expire=1&itag=140";
        check(job.start(p),"separate AAC+direct HLS plan stages");
        for(int i=0;i<12 && job.state()==playback::HlsCacheState::Fetching;++i)
            (void)job.pump();
        check(job.state()==playback::HlsCacheState::Ready &&
              sink.audio_appends==1 && sink.aac_bytes==2048 &&
              sink.publishes==1 && audio.requests==1,
              "AAC fully fetched with 206 range before video/audio commit");
    }
    {
        FakeTransport video;
        FakeAacTransport audio;audio.blocked=true;
        playback::HlsSegmentReader video_reader(video);
        playback::MediaChunkReader audio_reader(audio);
        Sink sink;
        playback::HlsCacheJob job(video_reader,sink,&audio_reader);
        auto p=plan();
        p.audio_segments.reset();
        p.selected.audio_playlist_url.clear();
        p.selected.direct_media_playlist=true;
        p.source.external_aac_url=
            "https://r2.googlevideo.com/videoplayback?expire=1&itag=140";
        check(job.start(p),"403 AAC failure fixture stages");
        for(int i=0;i<12 && job.state()==playback::HlsCacheState::Fetching;++i)
            (void)job.pump();
        check(job.state()==playback::HlsCacheState::Failed &&
              job.error()==playback::HlsCacheFailure::MediaFetch &&
              sink.publishes==0 && sink.aborts>0,
              "AAC 403 aborts cached video, not silently published");
    }
    {
        FakeTransport transport;
        playback::HlsSegmentReader reader(transport);
        Sink sink;playback::HlsCacheJob job(reader,sink);
        auto invalid=plan();invalid.video_segments.segments.clear();
        check(!job.start(invalid) && transport.hits==0,
              "empty video tracks blocked without external I/O");
    }
}
}
int main() {
    full();failure_cases();
    if(failures)std::cerr<<failures<<" HLS cache regressions\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
