#include "tizentube_nx/playback/hls_manifest.hpp"
#include "tizentube_nx/playback/vision_hls.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ttnx;
namespace {
int failures=0;
void check(bool good,const char* description) {
    if(!good){++failures;std::cerr<<"FAIL "<<description<<"\n";}
}
constexpr const char* kVideo="2kD9v7Q3vec";
constexpr const char* kMaster=
    "https://manifest.googlevideo.com/api/manifest/hls_playlist/expire/123/signature/SIGNED";
constexpr const char* kVariant=
    "https://manifest.googlevideo.com/api/manifest/hls_variant/itag/95/index.m3u8";
constexpr const char* kAudio=
    "https://manifest.googlevideo.com/api/manifest/hls_variant/audio/140.m3u8";
youtube::GuestSession guest() {
    youtube::GuestSession s;
    s.api_version="v1";s.client_name="WEB";s.client_version="2.20260901.00.00";
    s.client_name_id="1";s.visitor_data="opaque-guest-visitor";s.language="en";s.region="US";
    return s;
}
struct FakeHttp:net::HttpClient {
    int count{0};
    net::HttpRequest last;
    net::HttpResult response;
    net::HttpResult perform(const net::HttpRequest& request) override {
        ++count;last=request;return response;
    }
};
std::string player_json(std::string status="OK",std::string id=kVideo,
                        std::string url=kMaster) {
    return "{\"playabilityStatus\":{\"status\":\""+status+"\"},"
           "\"videoDetails\":{\"videoId\":\""+id+
           "\",\"lengthSeconds\":\"750\",\"isShort\":false,\"isLiveContent\":false},"
           "\"streamingData\":{\"hlsManifestUrl\":\""+url+"\"},"
           "\"adPlacements\":[{\"adPlacementRenderer\":{\"ignore\":\"tracking\"}}]}";
}
void player_request() {
    const auto session=guest();
    auto req=youtube::make_m3_visionos_player_request(session,kVideo);
    check(req && req->method==net::HttpMethod::Post &&
          req->url=="https://www.youtube.com/youtubei/v1/player?prettyPrint=false",
          "M3 player request stays on established guest-origin HTTPS");
    if(req) {
        check(req->body.find("\"clientName\":\"VISIONOS\"")!=std::string::npos &&
              req->body.find("\"visitorData\":\"opaque-guest-visitor\"")!=std::string::npos &&
              req->body.find("\"videoId\":\"2kD9v7Q3vec\"")!=std::string::npos,
              "reviewed client identity uses real guest visitor and canonical ID");
        check(req->body.find("serviceIntegrityDimensions")==std::string::npos &&
              req->body.find("adSignalsInfo")==std::string::npos &&
              req->body.find("contentCheckOk")==std::string::npos &&
              req->body.find("racyCheckOk")==std::string::npos,
              "no telemetry tokens or age restriction bypass fields");
        for(const auto& header:req->headers)
            check(header.name!="Cookie" && header.name!="Authorization",
                  "guest player never sends credentials");
    }
    check(!youtube::make_m3_visionos_player_request(session,
          "https://www.youtube.com/shorts/2kD9v7Q3vec"),
          "Shorts URL cannot become player ID");
    auto bad=guest();bad.client_name="ANDROID";
    check(!youtube::make_m3_visionos_player_request(bad,kVideo),
          "untrusted bootstrap guest identity cannot select the profile");
    FakeHttp http;
    http.response.ok=true;
    http.response.response.status_code=200;
    http.response.response.body=player_json();
    playback::VisionHlsSource extractor(http,session);
    const auto resolved=extractor.resolve(kVideo);
    check(resolved && resolved->master_playlist_url==kMaster &&
          resolved->video_id==kVideo && resolved->duration_ms==750000 &&
          http.count==1,"HLS VisionOS source validates id/status and duration");
    const std::string vision_aac=R"JSON({
      "playabilityStatus":{"status":"OK"},
      "videoDetails":{"videoId":"2kD9v7Q3vec","lengthSeconds":"750",
                      "isShort":false,"isLiveContent":false},
      "streamingData":{
        "hlsManifestUrl":"https://manifest.googlevideo.com/api/manifest/hls_variant/direct",
        "adaptiveFormats":[
          {"url":"https://accounts.nintendo.com/videoplayback",
           "mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","bitrate":256000},
          {"url":"https://r1.googlevideo.com/videoplayback?itag=140",
           "mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","bitrate":128000},
          {"url":"https://r2.googlevideo.com/videoplayback?itag=141",
           "mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","bitrate":192000}
        ]
      }
    })JSON";
    const auto paired=playback::parse_m3_guest_hls_source(vision_aac,kVideo);
    check(paired && paired->external_aac_url==
          "https://r2.googlevideo.com/videoplayback?itag=141" &&
          paired->master_playlist_url.find("/hls_variant/")!=std::string::npos,
          "same-video VisionOS direct HLS pairs highest reviewed signed AAC");
    auto blocked_audio=vision_aac;
    const auto token=blocked_audio.find("r2.googlevideo.com/videoplayback?itag=141");
    if(token!=std::string::npos)blocked_audio.replace(
       token,std::string("r2.googlevideo.com/videoplayback?itag=141").size(),
       "accounts.nintendo.com/videoplayback?itag=141");
    const auto paired_rejected=playback::parse_m3_guest_hls_source(blocked_audio,kVideo);
    check(paired_rejected &&
          paired_rejected->external_aac_url==
             "https://r1.googlevideo.com/videoplayback?itag=140",
          "unreviewed AAC host ignored without discarding safe fallback");
    http.response.response.body=player_json("LOGIN_REQUIRED");
    check(!extractor.resolve(kVideo) &&
          extractor.last_error()==playback::HlsSourceError::ResponseRejected,
          "guest restriction blocks HLS URL publication");
    http.response.response.body=player_json("OK","differentID");
    check(!extractor.resolve(kVideo),"upstream identity swap refused");
    http.response.response.body=player_json("OK",kVideo,
                                     "https://accounts.nintendo.com/api/manifest/hls_playlist/evil");
    check(!extractor.resolve(kVideo),"wrong HLS host refused");
    http.response.response.status_code=403;
    check(!extractor.resolve(kVideo) && extractor.last_error()==playback::HlsSourceError::HttpStatus,
          "403 is not a playable HLS manifest");
}
const std::string kPlaylist=
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",NAME=\"English\",URI=\""
      +std::string(kAudio)+"\"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=640x360,"
      "FRAME-RATE=30.000,CODECS=\"avc1.42001e,mp4a.40.2\",AUDIO=\"aac\"\n"
    "https://manifest.googlevideo.com/api/manifest/hls_variant/itag/93/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1800000,RESOLUTION=1280x720,"
      "FRAME-RATE=30.000,CODECS=\"avc1.64001f,mp4a.40.2\",AUDIO=\"aac\"\n"
    +std::string(kVariant)+"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3300000,RESOLUTION=1920x1080,"
      "FRAME-RATE=60.000,CODECS=\"avc1.640028,mp4a.40.2\",AUDIO=\"aac\"\n"
    "https://manifest.googlevideo.com/api/manifest/hls_variant/itag/96/index.m3u8\n";
void playlist_tests() {
    auto parsed=playback::parse_hls_master_playlist(kPlaylist,kMaster);
    check(parsed && parsed->compatible.size()==3,
          "bounded HLS master recognizes 3 explicit AVC/AAC variants");
    if(parsed) {
        auto choose=playback::choose_hls_variant(*parsed);
        check(choose && choose->height==720 && choose->fps==30 &&
              choose->playlist_url==kVariant && choose->audio_playlist_url==kAudio,
              "highest allowed 720p30 includes correctly paired AAC playlist");
        choose=playback::choose_hls_variant(*parsed,360,30);
        check(choose && choose->height==360,"360p fallback source preserved");
    }
    const std::string malicious=
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=500000,"
        "RESOLUTION=640x360,CODECS=\"avc1.42,mp4a.40.2\"\n"
        "https://accounts.nintendo.com/videoplayback\n";
    check(!playback::parse_hls_master_playlist(malicious,kMaster),
          "manifest cannot instruct a Nintendo CDN request");
    const std::string encoded_host=
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=500000,"
        "RESOLUTION=640x360,CODECS=\"avc1.42,mp4a.40.2\"\n"
        "https://manifest.googlevideo.com.attacker.tld/api/manifest/hls_variant/test\n";
    check(!playback::parse_hls_master_playlist(encoded_host,kMaster),
          "suffix confusion blocked");
    const std::string drm="#EXTM3U\n#EXT-X-SESSION-KEY:METHOD=AES-128,"
                          "URI=\"https://r.googlevideo.com/videoplayback\"\n";
    check(!playback::parse_hls_master_playlist(drm,kMaster),
          "DRM/session-key manifest denied");
    check(!playback::is_reviewable_hls_child_url(
       "https://r.googlevideo.com:444/videoplayback"),
       "alternate port blocked");
    check(!playback::is_reviewable_hls_child_url(
       "https://r.googlevideo.com/api/manifest/hls_variant/%2e%2e/x"),
       "percent-encoded traversal blocked");
}
void segment_tests() {
    const std::string valid=
        "#EXTM3U\n#EXT-X-VERSION:7\n"
        "#EXT-X-TARGETDURATION:6\n"
        "#EXT-X-MAP:URI=\"https://r1.googlevideo.com/videoplayback?init=1\"\n"
        "#EXTINF:6.000,\n"
        "https://r1.googlevideo.com/videoplayback?segment=1\n"
        "#EXTINF:5.500,\n"
        "https://r1.googlevideo.com/videoplayback?segment=2\n"
        "#EXT-X-ENDLIST\n";
    const auto parsed=playback::parse_hls_media_playlist(valid,kVariant);
    check(parsed && parsed->init_segment_url &&
          parsed->segments.size()==2 &&
          parsed->total_duration_ms==11500,
          "VOD media playlist contains reviewed init and segments");
    const auto no_end=valid.substr(0,valid.find("#EXT-X-ENDLIST"));
    check(!playback::parse_hls_media_playlist(no_end,kVariant),
          "unbounded live playlist not accepted as VOD");
    auto bad=valid;
    bad.insert(bad.find("#EXTINF:5.500"),"#EXT-X-BYTERANGE:123@456\n");
    check(!playback::parse_hls_media_playlist(bad,kVariant),
          "unsupported dependent byterange refused");
    bad=valid;
    bad.insert(bad.find("#EXTINF:5.500"),"#EXT-X-CUE-OUT:30\n");
    check(!playback::parse_hls_media_playlist(bad,kVariant),
          "ad insertion cue refused");
    bad=valid;
    const auto loc=bad.find("https://r1.googlevideo.com/videoplayback?segment=2");
    bad.replace(loc,std::string("https://r1.googlevideo.com/videoplayback?segment=2").size(),
                "https://evil.example/segment");
    check(!playback::parse_hls_media_playlist(bad,kVariant),
          "rogue segment destination refused");
}
} // namespace
int main() {
    player_request();playlist_tests();segment_tests();
    if(failures)std::cerr<<failures<<" HLS regressions\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
