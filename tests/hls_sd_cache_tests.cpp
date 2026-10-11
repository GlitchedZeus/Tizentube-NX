#include "hls_cache_sink.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace ttnx;
namespace fs=std::filesystem;
namespace {
int failures=0;
const fs::path base="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls";
void ck(bool condition,const char* description) {
    if(!condition){++failures;std::cerr<<"FAIL "<<description<<"\n";}
}
playback::HlsProbePlan plan() {
    playback::HlsProbePlan p;
    p.source.video_id="2kD9v7Q3vec";p.source.duration_ms=12000;
    p.selected.width=1280;p.selected.height=720;
    p.selected.fps=30;p.selected.bandwidth=1800000;
    p.selected.playlist_url="https://manifest.googlevideo.com/api/manifest/hls_variant/video";
    p.selected.audio_playlist_url="https://manifest.googlevideo.com/api/manifest/hls_variant/audio";
    p.video_segments.init_segment_url="https://r.googlevideo.com/videoplayback?init=v";
    p.video_segments.segments={
        {"https://r.googlevideo.com/videoplayback?v=1",6000},
        {"https://r.googlevideo.com/videoplayback?v=2",6000},
    };
    p.video_segments.total_duration_ms=12000;
    playback::HlsMediaPlaylist a;
    a.init_segment_url="https://r.googlevideo.com/videoplayback?init=a";
    a.segments={{"https://r.googlevideo.com/videoplayback?a=1",6000},
                {"https://r.googlevideo.com/videoplayback?a=2",6000}};
    a.total_duration_ms=12000;
    p.audio_segments=a;
    return p;
}
std::string read(const fs::path& file) {
    std::ifstream stream(file,std::ios::binary);
    if(!stream)return {};
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}
bool fully_stage(switch_app::SwitchHlsCacheSink& sink,
                 const playback::HlsProbePlan& p) {
    if(!sink.begin(p))return false;
    const std::string bytes("\0\0\0\x18ftypisom0000",16);
    if(!sink.write_segment(false,true,0,bytes))return false;
    if(!sink.write_segment(false,false,0,bytes))return false;
    if(!sink.write_segment(false,false,1,bytes))return false;
    if(!sink.write_segment(true,true,0,bytes))return false;
    if(!sink.write_segment(true,false,0,bytes))return false;
    if(!sink.write_segment(true,false,1,bytes))return false;
    return sink.publish(p);
}
}
int main() {
    fs::remove_all("/tmp/tizentube-m3-hls-test");
    fs::create_directories("/tmp/tizentube-m3-hls-test/switch");
    auto source=plan();
    {
        switch_app::SwitchHlsCacheSink sink;
        ck(fully_stage(sink,source),
           "complete HLS video/AAC clip publishes under fixed app folder");
        ck(fs::is_regular_file(base/"ready/master.m3u8") &&
           fs::is_regular_file(base/"ready/video.m3u8") &&
           fs::is_regular_file(base/"ready/audio.m3u8") &&
           fs::file_size(base/"ready/v_00000.m4s")==16 &&
           fs::file_size(base/"ready/a_00001.m4s")==16,
           "local playlist and separate media segments are actually written");
        const auto master=read(base/"ready/master.m3u8");
        const auto video=read(base/"ready/video.m3u8");
        const auto audio=read(base/"ready/audio.m3u8");
        ck(master.find("audio.m3u8")!=std::string::npos &&
           master.find("video.m3u8")!=std::string::npos &&
           master.find("googlevideo.com")==std::string::npos &&
           video.find("#EXT-X-MAP:URI=\"v_init.m4s\"")!=std::string::npos &&
           audio.find("#EXT-X-MAP:URI=\"a_init.m4s\"")!=std::string::npos,
           "generated HLS files reference only local media; no signed URL leak");
    }
    {
        switch_app::SwitchHlsCacheSink sink;
        ck(sink.begin(source),"new clip may stage while old is ready");
        ck(sink.write_segment(false,false,0,"incomplete"),
           "partial transfer may write only to pending directory");
        sink.abort();
        ck(fs::exists(base/"ready/master.m3u8") &&
           fs::exists(base/"ready/v_00000.m4s") &&
           !fs::exists(base/"pending"),
           "cancel preserves old completed media while removing partial files");
    }
    {
        // Simulate console power loss between the two directory renames.
        fs::rename(base/"ready",base/"previous");
        ck(!fs::exists(base/"ready"),"simulated interrupted publish");
        switch_app::SwitchHlsCacheSink sink;
        ck(sink.begin(source) &&
           fs::exists(base/"ready/master.m3u8") &&
           !fs::exists(base/"previous"),
           "begin recovers old completed clip after interrupted publish");
        sink.abort();
    }
    {
        switch_app::SwitchHlsCacheSink sink;
        ck(fully_stage(sink,source) &&
           fs::exists(base/"ready/master.m3u8") &&
           !fs::exists(base/"previous"),
           "new complete clip replaces ready and cleans old backup");
    }
    {
        // In HLS without EXT-X-MAP, preserve MPEG-TS segment file names
        // rather than pretending every stream is fragmented MP4.
        auto ts=plan();
        ts.selected.direct_media_playlist=true;
        ts.selected.height=0;
        ts.selected.width=0;
        ts.selected.fps=0;
        ts.video_segments.init_segment_url.reset();
        ts.audio_segments->init_segment_url.reset();
        switch_app::SwitchHlsCacheSink sink;
        ck(sink.begin(ts) &&
           sink.write_segment(false,false,0,"MPEG-TS-SAMPLE-VIDEO") &&
           sink.write_segment(false,false,1,"MPEG-TS-SAMPLE-VIDEO") &&
           sink.write_segment(true,false,0,"MPEG-TS-SAMPLE-AUDIO") &&
           sink.write_segment(true,false,1,"MPEG-TS-SAMPLE-AUDIO") &&
           sink.publish(ts),
           "direct HLS video and AAC rendition can stage local TS segment names");
        const auto vm=read(base/"ready/video.m3u8");
        const auto am=read(base/"ready/audio.m3u8");
        const auto master=read(base/"ready/master.m3u8");
        ck(fs::exists(base/"ready/v_00000.ts") &&
           fs::exists(base/"ready/a_00001.ts") &&
           vm.find("v_00000.ts")!=std::string::npos &&
           am.find("a_00001.ts")!=std::string::npos &&
           vm.find("#EXT-X-MAP:")==std::string::npos &&
           master.find("RESOLUTION=0x0")==std::string::npos &&
           master.find("CODECS=")==std::string::npos,
           "native cache uses TS extension and never invents direct-HLS codec metadata");
    }
    {
        auto paired=plan();
        paired.selected.direct_media_playlist=true;
        paired.selected.height=0;
        paired.selected.width=0;
        paired.selected.fps=0;
        paired.audio_segments.reset();
        paired.selected.audio_playlist_url.clear();
        paired.video_segments.init_segment_url.reset();
        paired.source.external_aac_url=
            "https://r1.googlevideo.com/videoplayback?itag=140";
        switch_app::SwitchHlsCacheSink sink;
        const std::string signed_audio_file(2048,'A');
        ck(sink.begin(paired) &&
           sink.write_segment(false,false,0,"TS-VIDEO-SEGMENT-A") &&
           sink.write_segment(false,false,1,"TS-VIDEO-SEGMENT-B") &&
           sink.append_external_aac(signed_audio_file) &&
           sink.publish(paired),
           "direct HLS+external AAC stages and publishes one local transaction");
        ck(fs::is_regular_file(base/"ready/external_audio.m4a") &&
           fs::file_size(base/"ready/external_audio.m4a")==2048 &&
           fs::is_regular_file(base/"ready/v_00000.ts"),
           "video TS and independent AAC MP4 both complete in ready directory");
        ck(read(base/"ready/master.m3u8").find("googlevideo.com")==std::string::npos &&
           read(base/"ready/master.m3u8").find("RESOLUTION=0x0")==std::string::npos,
           "signed audio URL is never leaked into local mpv playlist");
    }
    {
        // A short AAC download must never replace the previous good clip.
        auto paired=plan();
        paired.selected.direct_media_playlist=true;
        paired.audio_segments.reset();
        paired.selected.audio_playlist_url.clear();
        paired.source.external_aac_url=
            "https://r1.googlevideo.com/videoplayback?itag=140";
        switch_app::SwitchHlsCacheSink sink;
        ck(sink.begin(paired) &&
           sink.write_segment(false,false,0,"TS-VIDEO-SEGMENT-A") &&
           sink.append_external_aac("AUDIO_TOO_SHORT") &&
           !sink.publish(paired),
           "truncated AAC file blocks cache publication");
        sink.abort();
        ck(fs::is_regular_file(base/"ready/external_audio.m4a") &&
           !fs::exists(base/"pending"),
           "failed AAC staging preserves old fully published clip");
    }
    fs::remove_all("/tmp/tizentube-m3-hls-test");
    if(failures)std::cerr<<failures<<" SD cache contract failures\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
