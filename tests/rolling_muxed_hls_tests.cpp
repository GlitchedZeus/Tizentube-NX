#include "rolling_hls_feed.hpp"
#include "rolling_hls_runtime.hpp"
#include "rolling_hls_native_job.hpp"
#include "rolling_hls_worker.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using namespace ttnx;
using Feed=switch_app::RollingMuxedHlsSession;
using Sink=switch_app::RollingHlsSink;
namespace {
int errors=0;
void ck(bool ok,const char* msg) {
    if(!ok){++errors;std::cerr<<"FAIL "<<msg<<'\n';}
}
std::string ts(bool aac=true,std::size_t packets=2) {
    std::string b(188*packets,static_cast<char>(0xff));
    for(std::size_t i=0;i<packets;++i)b[i*188]=0x47;
    const auto put=[&](std::size_t at,std::initializer_list<unsigned char> v) {
        for(auto c:v)b[at++]=static_cast<char>(c);
    };
    put(0,{0x47,0x40,0x00,0x10,0,0x00,0xb0,0x0d,0,1,0xc1,
           0,0,0,1,0xe1,0,0,0,0,0});
    put(188,{0x47,0x41,0x00,0x10,0,0x02,0xb0,
             static_cast<unsigned char>(aac?23:18),
             0,1,0xc1,0,0,0xe1,1,0xf0,0,0x1b,0xe1,1,0xf0,0});
    if(aac)put(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return b;
}
struct LocalMedia : net::HttpClient {
    int hits=0, fail_at=0, delay_ms=0;
    bool aac=true,broken_sync=false;
    std::size_t packets=2;
    net::HttpResult perform(const net::HttpRequest& req) override {
        ++hits;
        if(delay_ms>0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        net::HttpResult o;
        o.ok=hits!=fail_at;
        o.response.status_code=o.ok?200:403;
        o.response.body=ts(aac,packets);
        if(broken_sync)o.response.body[188]=0x22;
        o.response.headers={{"Content-Type","video/mp2t"},
            {"Content-Length",std::to_string(o.response.body.size())}};
        ck(req.method==net::HttpMethod::Get &&
           req.url.find("googlevideo.com/")!=std::string::npos,
           "only approved CDN GET executed");
        return o;
    }
};
playback::HlsProbePlan plan(std::size_t count=6) {
    playback::HlsProbePlan p;
    p.source.video_id="2kD9v7Q3vec";
    p.source.duration_ms=count*3000;
    p.selected.height=720;
    p.selected.fps=60;
    p.selected.direct_media_playlist=true;
    p.video_segments.total_duration_ms=count*3000;
    for(std::size_t i=0;i<count;++i)
        p.video_segments.segments.push_back({
           "https://r1.googlevideo.com/videoplayback?itag=22&part="+std::to_string(i),3000});
    return p;
}
void reset() {fs::remove_all("/tmp/ttnx-m4-rolling-test");}

struct StreamOnlyMedia final : playback::HlsSegmentStreamTransport {
    std::atomic<int> calls{0};
    int fail_at=0;
    playback::HlsStreamResult stream_segment(
        std::string_view url,const playback::HlsMediaAdmission& admit,
        const playback::HlsMediaChunk& emit) override {
        if(!playback::is_reviewable_hls_child_url(url))return {};
        const int current=++calls;
        const std::string media=ts(true,6000);
        if(!admit(media.size()))return {};
        for(std::size_t at=0;at<media.size();at+=32768) {
            if(current==fail_at && at>=32768)return {};
            if(!emit(std::string_view(media).substr(
                  at,std::min<std::size_t>(32768,media.size()-at))))return {};
        }
        return {true,media.size()};
    }
};
void physical_quota_and_closed_decoder_window() {
    reset();
    LocalMedia legacy;
    StreamOnlyMedia streaming;
    playback::HlsSegmentReader reader(legacy,&streaming);
    Sink sink;
    Feed feed(reader,sink);
    ck(feed.begin(plan(14)), "start 14-segment physical-quota session");
    // ~1.128 MiB each: before this fix a permanent 16-MiB reservation
    // blocked the 9th small segment with only ~8 MiB on SD.
    for(unsigned i=0;i<11;++i) {
        const auto next=feed.pump();
        ck(next==Feed::State::PrefixReady && feed.downloaded_segments()==i+1,
           "exact HTTP Content-Length admits small SD segments beyond 8 MiB");
    }
    ck(sink.bytes()>8ULL*1024*1024 &&
       sink.bytes()<=Sink::kMaxMediaBytes &&
       legacy.hits==0 && streaming.calls.load()==11,
       "actual media bytes are bounded without any whole-response fetch");
    ck(feed.pump()==Feed::State::Waiting &&
       streaming.calls.load()==11,
       "30-second prefetch window waits without an unauthorized HTTP request");
    {
        playback::HlsPlaybackLease lease;
        ck(lease.acquire() && !feed.retire_before(3,30000,true) &&
           fs::exists(fs::path(Sink::kRoot)/"v_00000.ts"),
           "decoder lease vetoes stale segment deletion even if caller lies");
    }
    ck(!feed.retire_before(3,7000,true) &&
       fs::exists(fs::path(Sink::kRoot)/"v_00000.ts"),
       "insufficient playhead guard cannot mutate retained window");
    ck(feed.retire_before(3,30000,true) &&
       !fs::exists(fs::path(Sink::kRoot)/"v_00000.ts"),
       "only after confirmed decoder closure can early segments be deleted");
    for(unsigned i=11;i<14;++i)
        ck(feed.pump()==Feed::State::PrefixReady,
           "producer resumes after safe release and fetches future segments");
    ck(feed.pump()==Feed::State::Complete &&
       streaming.calls.load()==14 &&
       sink.bytes()<=Sink::kMaxMediaBytes,
       "future H264/AAC segments finish below disposable quota");
    ck(feed.close_after_player(true) && !fs::exists(Sink::kRoot),
       "full finite playback window purge leaves zero SD media");
}

void native_direct_to_sd_stream() {
    reset();
    LocalMedia legacy;
    StreamOnlyMedia streaming;
    // The M4 runtime accepts an explicit native stream transport. No
    // legacy HttpClient::perform() calls or 16MiB copies are permitted.
    switch_app::RollingHlsRuntime runtime(legacy,&streaming);
    ck(runtime.begin(plan(3)), "start native streaming transport fixture");
    for(unsigned i=0;i<1000 && runtime.snapshot().published_segments<3;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ck(streaming.calls.load()==3 && legacy.hits==0 &&
       runtime.snapshot().published_segments==3,
       "all 3 segments streamed with zero whole-response fetches");
    ck(runtime.stored_media_bytes()>0 &&
       runtime.stored_media_bytes()<=Sink::kMaxMediaBytes,
       "native streaming enters strictly bounded SD cache");
    runtime.request_stop();
    runtime.join_downloads();
    ck(runtime.cleanup() && !fs::exists(Sink::kRoot),
       "native streaming session tears down all downloaded media");

    reset();
    LocalMedia untouched;
    StreamOnlyMedia broken;broken.fail_at=2;
    switch_app::RollingHlsRuntime failing(untouched,&broken);
    ck(failing.begin(plan(3)), "begin intentional mid-body receive failure");
    for(unsigned i=0;i<1000 && broken.calls.load()<2;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    failing.request_stop();
    failing.join_downloads();
    ck(untouched.hits==0 && broken.calls.load()>=2,
       "mid-body HTTP failure never retries via memory-buffered fallback");
    ck(failing.cleanup() && !fs::exists(Sink::kRoot),
       "failed streaming producer discards all private .part media");
}

void successful() {
    reset();
    LocalMedia http;
    playback::HlsSegmentReader reader(http);
    Sink disk;
    Feed work(reader,disk);
    ck(work.begin(plan()) && http.hits==0,"begin validates plan without a network call");
    ck(work.pump()==Feed::State::PrefixReady && work.prefix_prepared() &&
       http.hits==1 && work.downloaded_segments()==1 &&
       fs::exists(fs::path(Sink::kRoot)/"v_00000.ts"),
       "real reviewed HTTP reader writes first verified muxed segment and local live manifest");
    const fs::path playlist=fs::path(Sink::kRoot)/"stream.m3u8";
    std::ifstream file(playlist);
    std::string text((std::istreambuf_iterator<char>(file)),{});
    ck(text.find("googlevideo.com")==std::string::npos &&
       text.find("expire=")==std::string::npos &&
       text.find("v_00000.ts")!=std::string::npos,
       "published manifest contains no signed URLs or tracking tokens");
    for(unsigned i=1;i<6;++i)
        ck(work.pump()==Feed::State::PrefixReady,
           "worker fetches and publishes one verified segment per pump");
    ck(work.pump()==Feed::State::Complete && work.downloaded_segments()==6,
       "completed playlist gets ENDLIST only after all six segments");
    ck(work.retire_before(2,16000,true) &&
       !fs::exists(fs::path(Sink::kRoot)/"v_00000.ts") &&
       !fs::exists(fs::path(Sink::kRoot)/"v_00001.ts"),
       "decoder-certified consumed files retired after guard");
    ck(!work.close_after_player(false),"cannot clear media while decoder is active");
    {
        playback::HlsPlaybackLease lease;
        ck(lease.acquire() && !work.close_after_player(true) &&
           fs::exists(Sink::kRoot),
           "even a false release claim cannot override an active libmpv lease");
    }
    ck(work.close_after_player(true) && !fs::exists(Sink::kRoot),
       "worker complete+decoder closed leaves zero saved media");
    ck(work.begin(plan()) && work.pump()==Feed::State::PrefixReady,
       "second video reuses the same worker and sink after full cleanup");
    ck(work.close_after_player(true) && !fs::exists(Sink::kRoot),
       "repeated playback again ends with no downloaded media");
}
void threaded() {
    reset();
    LocalMedia http;
    http.packets=6000; // actual verified 1.128 MiB TS per HTTPS response
    playback::HlsSegmentReader reader(http);
    Sink disk;
    Feed feed(reader,disk);
    ck(feed.begin(plan(16)),"native HLS job validates its VOD source");
    switch_app::RollingMuxedHlsWorkerJob job(feed);
    switch_app::RollingHlsWorker worker;
    ck(worker.start(job),"real muxed HLS producer begins on background thread");
    const auto await=[&](auto ready) {
        for(unsigned i=0;i<5000;++i) {
            if(ready())return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return ready();
    };
    using State=switch_app::RollingHlsWorker::State;
    ck(await([&]{return worker.snapshot().state==State::Waiting;}),
       "reviewed native HTTP source reaches bounded backpressure");
    const auto snap=worker.snapshot();
    ck(snap.prefix_ready && snap.published_segments>=4,
       "first A/V prefix is safely reported without blocking the UI");
    ck(!worker.request_retirement(3,40000,false),
       "unverified decoder release never schedules deletion");
    ck(worker.request_retirement(3,40000,true),
       "verified consumed segment window schedules worker-owned unlink");
    ck(await([&]{return worker.snapshot().published_segments>
                         snap.published_segments;}),
       "real HTTPS fetch resumes after actual SD segment retirement");
    worker.request_stop();
    worker.join_downloads();
    {
        playback::HlsPlaybackLease lease;
        ck(lease.acquire() && !worker.cleanup_after_decoder_closed(),
           "even joined worker cannot delete media with active player lease");
    }
    ck(worker.cleanup_after_decoder_closed() && !fs::exists(Sink::kRoot) &&
       worker.snapshot().state==State::Cleared,
       "complete downloader join and decoder close leaves zero SD media");
}
void full_native_owner() {
    reset();
    LocalMedia http;
    http.delay_ms=60; // emulate a real CDN instead of instant test responses
    switch_app::RollingHlsRuntime runtime(http);
    ck(runtime.begin(plan()),"native owner validates video and starts async feed");
    using State=switch_app::RollingHlsWorker::State;
    const auto until=[&](auto predicate) {
        for(unsigned i=0;i<5000;++i) {
            if(predicate())return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return predicate();
    };
    ck(until([&]{return runtime.snapshot().state==State::PrefixReady &&
                         runtime.snapshot().published_segments>=1;}),
       "initial verified A/V prefix available while later HTTPS segments fetch");
    ck(runtime.snapshot().published_segments<6 &&
       runtime.stored_media_bytes()>0,
       "3-second startup prefix does NOT require complete VOD downloading");
    ck(runtime.prepare_decoder() && playback::HlsPlaybackLease::is_active(),
       "decoder opens with lease before the worker finishes fetching");
    ck(until([&]{return runtime.snapshot().state==State::Complete;}),
       "network producer finishes remaining TS while player lease is active");
    ck(runtime.snapshot().prefix_ready,
       "completed stream preserves initial verified prefix");
    ck(!runtime.cleanup(),"cannot erase video while renderer still has FD access");
    runtime.request_stop();runtime.join_downloads();
    ck(!runtime.cleanup(),"download join alone cannot delete active mpv media");
    ck(runtime.decoder_destroyed() && !playback::HlsPlaybackLease::is_active(),
       "real native player-destroy signal releases session lease");
    ck(runtime.cleanup() && !runtime.active() &&
       !fs::exists(Sink::kRoot),"session safely tears down verified media");
    ck(runtime.begin(plan()),"same native owner reopens next video from scratch");
    runtime.request_stop();runtime.join_downloads();
    ck(runtime.cleanup() && !fs::exists(Sink::kRoot),
       "early cancellation leaves zero private SD cache");
}
void negative() {
    reset();
    {
        LocalMedia http;
        playback::HlsSegmentReader reader(http);
        Sink disk;Feed feed(reader,disk);
        auto p=plan();p.selected.height=1080;
        ck(!feed.begin(p) && http.hits==0,"never fetch above 720p");
        p=plan();p.audio_segments=playback::HlsMediaPlaylist{};
        ck(!feed.begin(p) && http.hits==0,
           "separate HLS AAC requires a different reviewed implementation");
        p=plan();p.video_segments.segments[0].url="https://evil.example.net/bad";
        ck(!feed.begin(p) && http.hits==0,"reject unreviewed media host");
    }
    {
        LocalMedia http;http.aac=false;
        playback::HlsSegmentReader reader(http);
        Sink disk;Feed feed(reader,disk);
        ck(feed.begin(plan()) && feed.pump()==Feed::State::Failed &&
           feed.error()==Feed::Error::Codec && http.hits==1,
           "TS without AAC rejected, never advertised as ready");
        ck(feed.close_after_player(true) && !fs::exists(Sink::kRoot),
           "failed codec fetch leaves no media behind");
    }
    {
        LocalMedia http;http.fail_at=2;
        playback::HlsSegmentReader reader(http);
        Sink disk;Feed feed(reader,disk);
        ck(feed.begin(plan()) && feed.pump()==Feed::State::PrefixReady &&
           feed.pump()==Feed::State::Failed &&
           feed.error()==Feed::Error::SegmentHttp && http.hits==2,
           "second-segment 403 stops, no blind retry and no false complete");
        ck(feed.close_after_player(true) && !fs::exists(Sink::kRoot),
           "HTTP failure removes already downloaded media after close");
    }
    {
        LocalMedia http;http.broken_sync=true;
        playback::HlsSegmentReader reader(http);
        Sink disk;Feed feed(reader,disk);
        ck(feed.begin(plan()) && feed.pump()==Feed::State::Failed &&
           feed.error()==Feed::Error::Codec,
           "bad TS alignment rejected before SD publish");
        ck(feed.close_after_player(true),"bad TS temporary files discarded");
    }
}
void bounded() {
    reset();
    LocalMedia http;http.packets=6000; // 1.128 MiB per segment
    playback::HlsSegmentReader reader(http);
    Sink disk;Feed feed(reader,disk);
    ck(feed.begin(plan(16)),"high-bitrate 720p session stages");
    bool waited=false;
    for(int i=0;i<16;++i) {
        const auto before=http.hits;
        const auto state=feed.pump();
        ck(feed.stored_bytes()<=Sink::kMaxMediaBytes,"SD cap never exceeded");
        if(state==Feed::State::Waiting) {
            ck(http.hits==before,"backpressure performs zero wasted HTTP calls");
            waited=true;
            break;
        }
    }
    ck(waited && feed.downloaded_segments()>2,
       "producer blocks when next 16 MiB reservation does not fit");
    const auto before=feed.downloaded_segments();
    ck(feed.retire_before(before-2, 40000, true),"retire old frames releases budget");
    ck(feed.pump()==Feed::State::PrefixReady &&
       feed.downloaded_segments()==before+1,
       "network fetch resumes after physical segment eviction");
    ck(feed.close_after_player(true) && !fs::exists(Sink::kRoot),
       "high-bitrate aborted playback leaves no temporary media");
}
}
int main() {
    successful();negative();bounded();threaded();full_native_owner();
    native_direct_to_sd_stream();
    physical_quota_and_closed_decoder_window();
    reset();
    if(errors)std::cerr<<errors<<" rolling muxed transport failures\n";
    return errors?EXIT_FAILURE:EXIT_SUCCESS;
}
