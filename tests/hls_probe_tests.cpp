#include "tizentube_nx/playback/hls_probe.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace ttnx;
namespace {
int errors=0;
void check(bool yes,const char* s) {
    if(!yes) {++errors;std::cerr<<"FAIL "<<s<<"\n";}
}
constexpr const char* kId="2kD9v7Q3vec";
constexpr const char* kMaster=
    "https://manifest.googlevideo.com/api/manifest/hls_playlist/expiry/123";
constexpr const char* kVideo=
    "https://manifest.googlevideo.com/api/manifest/hls_variant/video720.m3u8";
constexpr const char* kAudio=
    "https://manifest.googlevideo.com/api/manifest/hls_variant/audio140.m3u8";
constexpr const char* kSegmentBase=
    "https://r1.googlevideo.com/videoplayback?";
const std::string kPlayer=
    std::string("{\"playabilityStatus\":{\"status\":\"OK\"},")
    +"\"videoDetails\":{\"videoId\":\""+kId
    +"\",\"lengthSeconds\":\"12\",\"isShort\":false,\"isLiveContent\":false},"
    "\"streamingData\":{\"hlsManifestUrl\":\""+kMaster+"\"}}";
const std::string kMasterData=
    "#EXTM3U\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"English\",URI=\""
    +std::string(kAudio)+"\"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1800000,RESOLUTION=1280x720,"
    "FRAME-RATE=30.000,CODECS=\"avc1.64001f,mp4a.40.2\",AUDIO=\"audio\"\n"
    +std::string(kVideo)+"\n";
std::string media(std::string_view track) {
    return std::string("#EXTM3U\n#EXT-X-VERSION:7\n")
        +"#EXT-X-MAP:URI=\""+std::string(kSegmentBase)+std::string(track)+"init\"\n"
        +"#EXTINF:6.000,\n"+std::string(kSegmentBase)+std::string(track)+"1\n"
        +"#EXTINF:6.000,\n"+std::string(kSegmentBase)+std::string(track)+"2\n"
        +"#EXT-X-ENDLIST\n";
}
struct GuestHttp:net::HttpClient {
    int hits{0};
    bool blocked{false};
    net::HttpResult perform(const net::HttpRequest& r) override {
        ++hits;
        net::HttpResult response;
        response.ok=true;
        response.response.status_code=blocked?403:200;
        response.response.body=kPlayer;
        if(r.method!=net::HttpMethod::Post ||
           r.url!="https://www.youtube.com/youtubei/v1/player?prettyPrint=false")
            response.ok=false;
        return response;
    }
};
struct MediaHttp:net::HttpClient {
    int hits{0};
    int fail_on{0};
    int bad_mime_on{0};
    int bad_length_on{0};
    int bad_status_on{0};
    bool protect_video{false};
    bool audio_duration_mismatch{false};
    net::HttpResult perform(const net::HttpRequest& r) override {
        ++hits;
        net::HttpResult out;
        out.ok=true;out.response.status_code=hits==bad_status_on?302:200;
        if(r.method!=net::HttpMethod::Get ||
           r.headers.size()!=2 ||
           r.headers[1].name!="Accept-Encoding" ||
           r.headers[1].value!="identity")
            out.ok=false;
        if(hits==fail_on)out.ok=false;
        if(r.url==kMaster)out.response.body=kMasterData;
        else if(r.url==kVideo) {
            out.response.body=media("v");
            if(protect_video) {
                const auto at=out.response.body.find("#EXTINF:6.000");
                out.response.body.insert(at,
                    "#EXT-X-KEY:METHOD=AES-128,URI=\"https://r.googlevideo.com/videoplayback\"\n");
            }
        } else if(r.url==kAudio) {
            out.response.body=media("a");
            if(audio_duration_mismatch) {
                const auto at=out.response.body.find("#EXTINF:6.000");
                out.response.body.replace(at,14,"#EXTINF:60.000");
            }
        } else out.ok=false;
        out.response.headers={
            {"Content-Type",hits==bad_mime_on?"text/html":"application/vnd.apple.mpegurl"},
            {"Content-Length",
             std::to_string(out.response.body.size()+(hits==bad_length_on?1:0))}
        };
        return out;
    }
};
youtube::GuestSession guest() {
    youtube::GuestSession s;
    s.client_name="WEB";s.api_version="v1";
    s.client_version="2.20260901.00.00";s.visitor_data="safe-visitor";
    return s;
}
void full_path() {
    GuestHttp yt;MediaHttp cdn;
    auto session=guest();
    playback::HlsSourceProbe probe(yt,cdn,session);
    const auto data=probe.inspect(kId);
    check(data && data->selected.height==720 &&
          data->selected.audio_playlist_url==kAudio &&
          data->source.video_id==kId,
          "guest -> HLS master -> variant -> independent audio route selected");
    check(data && data->video_segments.segments.size()==2 &&
          data->audio_segments && data->audio_segments->segments.size()==2 &&
          data->audio_segments->total_duration_ms==12000,
          "paired HLS video and audio have compatible finite VOD durations");
    check(yt.hits==1 && cdn.hits==3,
          "one guest player plus three media manifests, no arbitrary network calls");
}
void blocked() {
    auto session=guest();
    {
        GuestHttp yt;MediaHttp cdn;yt.blocked=true;
        playback::HlsSourceProbe probe(yt,cdn,session);
        check(!probe.inspect(kId) && cdn.hits==0,
              "guest 403 fails before any CDN requests");
    }
    {
        GuestHttp yt;MediaHttp cdn;cdn.bad_status_on=1;
        playback::HlsSourceProbe probe(yt,cdn,session);
        check(!probe.inspect(kId) &&
              probe.last_error()==playback::HlsProbeError::ManifestStatus &&
              cdn.hits==1,"manifest redirect never followed");
    }
    {
        GuestHttp yt;MediaHttp cdn;cdn.bad_mime_on=2;
        playback::HlsSourceProbe probe(yt,cdn,session);
        check(!probe.inspect(kId) && cdn.hits==2,
              "HTML content cannot masquerade as selected video playlist");
    }
    {
        GuestHttp yt;MediaHttp cdn;cdn.bad_length_on=1;
        playback::HlsSourceProbe probe(yt,cdn,session);
        check(!probe.inspect(kId) && cdn.hits==1,
              "mismatched header/body length aborts");
    }
    {
        GuestHttp yt;MediaHttp cdn;cdn.protect_video=true;
        playback::HlsSourceProbe probe(yt,cdn,session);
        check(!probe.inspect(kId) &&
              probe.last_error()==playback::HlsProbeError::VideoSegmentsRejected,
              "encryption marker aborts before audio request");
    }
    {
        GuestHttp yt;MediaHttp cdn;cdn.audio_duration_mismatch=true;
        playback::HlsSourceProbe probe(yt,cdn,session);
        check(!probe.inspect(kId) && !probe.inspect("/shorts/2kD9v7Q3vec"),
              "drifted A/V duration and Shorts URL both rejected");
    }
}
} // namespace

int main() {
    full_path();blocked();
    if(errors)std::cerr<<errors<<" HLS source plan failures\n";
    return errors?EXIT_FAILURE:EXIT_SUCCESS;
}
