#include "tizentube_nx/playback/source_routes.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using namespace ttnx::playback;

namespace {
int problems=0;
void require(bool ok,const char* name) {
    if(!ok){++problems;std::cerr<<"FAIL "<<name<<"\n";}
}
constexpr const char* kVideo="https://r1.googlevideo.com/videoplayback?expire=123&sig=SECRET";
constexpr const char* kAudio="https://r2.googlevideo.com/videoplayback?expire=123&sig=SECRET";
constexpr const char* kHls="https://manifest.googlevideo.com/api/manifest/hls_playlist/expire/123/signature/SIGNATURE";
VideoSourceSet sample() {
    VideoSourceSet s;
    s.video_id="2kD9v7Q3vec";s.duration_ms=420000;
    s.candidates={
        {VideoDelivery::ProgressiveMp4,kVideo,"",360,30,true,true,false,false},
        {VideoDelivery::SeparateAv,kVideo,kAudio,720,30,true,true,false,false},
        {VideoDelivery::Hls,kHls,"",720,30,true,true,false,false}
    };
    return s;
}
void happy_paths() {
    auto s=sample();
    auto r=choose_source_route(s);
    require(r.planned.has_value() && r.failure==SourceRouteFailure::None &&
            r.planned->delivery==VideoDelivery::Hls &&
            r.planned->height==720 && r.planned->audio_url.empty(),
            "HLS preferred at max 720p30");
    s.candidates.pop_back();
    r=choose_source_route(s);
    require(r.planned && r.planned->delivery==VideoDelivery::SeparateAv &&
            r.planned->audio_url==kAudio,
            "paired H264/AAC adaptive source preferred when HLS absent");
    s.candidates.pop_back();
    r=choose_source_route(s);
    require(r.planned && r.planned->delivery==VideoDelivery::ProgressiveMp4 &&
            r.planned->height==360,
            "self-contained MP4 safe fallback without stripping audio");
    s=sample();
    r=choose_source_route(s,360,30);
    require(r.planned && r.planned->height==360,
            "explicit 360p quality maximum respected");
}
void deny_unsafe_sources() {
    const std::vector<std::string> bad={
        "http://manifest.googlevideo.com/api/manifest/hls_playlist/test",
        "https://accounts.nintendo.com/api/manifest/hls_playlist/test",
        "https://manifest.googlevideo.com.evil.tld/api/manifest/hls_playlist/test",
        "https://user@manifest.googlevideo.com/api/manifest/hls_playlist/test",
        "https://manifest.googlevideo.com:444/api/manifest/hls_playlist/test",
        "https://manifest.googlevideo.com/redirect",
        "https://manifest.googlevideo.com/api/manifest/hls_playlist/../../watch",
        "https://manifest.googlevideo.com/api/manifest/hls_playlist/test#fragment",
        "https://manifest.googlevideo.com/api/manifest/hls_playlist/test\nextra",
        "https://manifest.googlevideo.com/api/manifest/hls_playlist/test\\evil",
        "https://manifest.googlevideo.com//api/manifest/hls_playlist/test",
    };
    for(const auto& u:bad)require(!is_reviewable_hls_manifest_url(u),
                                "malicious HLS URL refused");
    require(is_reviewable_hls_manifest_url(kHls), "legitimate manifest host approved");
    auto s=sample();
    s.is_short=true;
    require(choose_source_route(s).failure==SourceRouteFailure::ShortsForbidden,
            "Shorts never routed to player");
    s.is_short=false;
    s.is_live=true;
    require(choose_source_route(s).failure==SourceRouteFailure::RestrictedOrDrm,
            "live requires separate future review");
    s.is_live=false;s.requires_auth=true;
    require(choose_source_route(s).failure==SourceRouteFailure::RestrictedOrDrm,
            "guest path cannot silently bypass authentication");
    s.requires_auth=false;s.duration_ms=0;
    require(choose_source_route(s).failure==SourceRouteFailure::InvalidDuration,
            "unknown duration denied");
    s.duration_ms=420000;s.video_id="a_short_url";
    require(choose_source_route(s).failure==SourceRouteFailure::InvalidId,
            "only canonical 11-character video IDs allowed");
    s.video_id="2kD9v7Q3vec";
    for(auto& c:s.candidates)c.drm=true;
    require(choose_source_route(s).failure==SourceRouteFailure::NoReviewedMediaSource,
            "all DRM sources blocked");
    s=sample();
    s.candidates[0].video_url="https://accounts.nintendo.com/videoplayback";
    s.candidates[1].audio_url="https://googlevideo.com/videoplayback";
    s.candidates[2].video_url="https://manifest.googlevideo.com/not-hls";
    require(!choose_source_route(s).planned,
            "poisoned destination never chosen");
}
}
int main() {
    happy_paths();deny_unsafe_sources();
    if(problems)std::cerr<<problems<<" media route regressions\n";
    return problems?EXIT_FAILURE:EXIT_SUCCESS;
}
