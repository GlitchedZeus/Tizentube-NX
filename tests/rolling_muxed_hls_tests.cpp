#include "rolling_hls_feed.hpp"
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
    int hits=0, fail_at=0;
    bool aac=true,broken_sync=false;
    std::size_t packets=2;
    net::HttpResult perform(const net::HttpRequest& req) override {
        ++hits;
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
    ck(work.close_after_player(true) && !fs::exists(Sink::kRoot),
       "worker complete+decoder closed leaves zero saved media");
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
    successful();negative();bounded();
    reset();
    if(errors)std::cerr<<errors<<" rolling muxed transport failures\n";
    return errors?EXIT_FAILURE:EXIT_SUCCESS;
}
