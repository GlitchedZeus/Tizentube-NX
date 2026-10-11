#include "tizentube_nx/playback/guest_direct_extractor.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ttnx::playback;
namespace {
int failures=0;
void check(bool yes,const char* msg) {
    if (!yes) {++failures;std::cerr<<"FAIL "<<msg<<"\n";}
}
const std::string fixture = R"JSON({
  "playabilityStatus":{"status":"OK","contextParams":"not-sent"},
  "adPlacements":[{"adPlacementRenderer":{"config":{"adSlot":"NEVER_OPEN_THIS"}}}],
  "videoDetails":{"videoId":"IzJ7R4EnYmI","lengthSeconds":"312","isLiveContent":false,"isShort":false,
    "title":"A test video \u00a9 with escaped unicode","viewCount":"100000"},
  "streamingData":{
     "expiresInSeconds":"10000",
     "formats":[{
       "itag":18,
       "url":"https://r1---sn-test.googlevideo.com/videoplayback?expire=123456\u0026sig=abc",
       "mimeType":"video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"",
       "width":640,"height":360,"fps":30,"bitrate":512000,
       "qualityLabel":"360p","audioChannels":2
     }],
     "adaptiveFormats":[{
       "url":"https://r2.googlevideo.com/videoplayback?expire=123456\u0026sig=def",
       "mimeType":"audio/mp4; codecs=\"mp4a.40.2\"",
       "bitrate":128000,"audioChannels":2
     },{
       "url":"https://r3.googlevideo.com/videoplayback?expire=123456",
       "mimeType":"video/mp4; codecs=\"avc1.64001f\"",
       "width":1280,"height":720,"fps":30,"bitrate":1300000
     },{
       "signatureCipher":"url%3Dhttps%3A%2F%2Funsafe%2F",
       "mimeType":"video/mp4; codecs=\"avc1.64001f\"",
       "width":1920,"height":1080,"fps":30,"bitrate":2700000
     },{
       "url":"https://accounts.nintendo.com/videoplayback",
       "mimeType":"video/mp4; codecs=\"avc1.64001f\"",
       "width":1920,"height":1080,"fps":30,"bitrate":2700000
     },{
       "url":"https://r4.googlevideo.com/videoplayback",
       "mimeType":"video/mp4; codecs=\"avc1.64001f\"",
       "width":1920,"height":1080,"fps":30,"bitrate":2700000,
       "drmFamilies":["widevine"]
     }]
  },
  "playbackTracking":{"videostatsPlaybackUrl":{"baseUrl":"https://ads.test/ad"}},
  "messages":[{"unicode":"\\uD83E\\uDDD0"}]
})JSON";
struct FakeHttp final : ttnx::net::HttpClient {
    int calls{0};
    ttnx::net::HttpRequest request;
    std::string body{fixture};
    bool succeed{true};
    ttnx::net::HttpResult perform(const ttnx::net::HttpRequest& r) override {
        ++calls;request=r;
        ttnx::net::HttpResult result;
        result.ok=succeed;
        result.response.status_code=succeed?200:503;
        result.response.body=body;
        return result;
    }
};
ttnx::youtube::GuestSession guest() {
    ttnx::youtube::GuestSession x;
    x.api_version="v1";x.client_name="WEB";x.client_version="2.20260901.00.00";
    x.visitor_data="anonymous-test";x.user_agent="TizenTube-NX-Hardware-Tests";
    return x;
}
std::string substitute(std::string source,std::string from,std::string to) {
    auto at=source.find(from);
    if(at!=std::string::npos)source.replace(at,from.size(),to);
    return source;
}
void normal() {
    const auto data=parse_direct_guest_player(fixture,"IzJ7R4EnYmI");
    check(data.has_value(),"direct guest metadata parsed");
    if(!data)return;
    check(data->video_id=="IzJ7R4EnYmI" && data->duration_ms==312000,
          "source identity and strict duration");
    check(data->video.size()==2 && data->audio.size()==1,
          "only H264/AAC direct streams retained, cipher+Nintendo+DRM excluded");
    check(data->video[0].muxed_audio==AudioCodec::Aac &&
          data->video[0].url.find("&sig=abc")!=std::string::npos,
          "JSON-escaped ampersand in signed Googlevideo url decoded");
    check(data->video[1].height==720 &&
          data->audio[0].codec==AudioCodec::Aac,
          "adaptive H264 720p and AAC track normalized");
    QualityPolicy progressive;
    progressive.prefer_adaptive=false;
    auto selected=select_streams(*data,progressive);
    check(selected.streams.has_value() &&
          selected.streams->video_codec==VideoCodec::H264,
          "native player metadata drives existing stream selector");

    FakeHttp http;
    auto s=guest();
    GuestDirectExtractor extractor(http,s);
    auto from_http=extractor.resolve("IzJ7R4EnYmI");
    check(from_http.has_value() && http.calls==1 &&
          http.request.url=="https://www.youtube.com/youtubei/v1/player?prettyPrint=false&alt=json",
          "guest player extractor uses approved M2 native HTTPS origin");
    http.succeed=false;
    check(!extractor.resolve("IzJ7R4EnYmI"),
          "failed HTTP never becomes usable source");
    check(!extractor.resolve("https://www.youtube.com/shorts/IzJ7R4EnYmI") &&
          http.calls==2,"arbitrary Shorts URL never makes player request");
}
void negative() {
    check(!parse_direct_guest_player(fixture,"other_ID11"),
          "upstream identity mismatches rejected");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"status\":\"OK\"","\"status\":\"LOGIN_REQUIRED\""),
        "IzJ7R4EnYmI"),"login required never bypassed");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"isShort\":false","\"isShort\":true"),
        "IzJ7R4EnYmI"),"shorts explicitly rejected");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"isLiveContent\":false","\"isLiveContent\":true"),
        "IzJ7R4EnYmI"),"live tracks require distinct future transport");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"videoId\":\"IzJ7R4EnYmI\"",
                "\"videoId\":\"IzJ7R4EnYmI\",\"videoId\":\"IzJ7R4EnYmI\""),
        "IzJ7R4EnYmI"),"duplicate video identity rejected");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"lengthSeconds\":\"312\"",
                "\"lengthSeconds\":\"99999999999999999999\""),
        "IzJ7R4EnYmI"),"duration overflow rejected");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"status\":\"OK\"",
                "\"status\":\"OK\",\"status\":\"OK\""),
        "IzJ7R4EnYmI"),"duplicate playability status rejected");
    check(!parse_direct_guest_player(
        substitute(fixture,"\"streamingData\":", "\"other\":true,\"streamingData\":"),
        "IzJ7R4EnYmI") == false, "extra safe metadata does not veto response");
    check(!parse_direct_guest_player(std::string(1024*1024+1,'X'),"IzJ7R4EnYmI"),
          "bounded response size");
    check(!parse_direct_guest_player("{","IzJ7R4EnYmI"),
          "malformed JSON rejected");
}
} // namespace
int main() {
    normal();negative();
    if(failures)std::cerr<<failures<<" guest player tests failed\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
