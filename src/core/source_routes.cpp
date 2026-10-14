#include "tizentube_nx/playback/source_routes.hpp"
#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/core/url.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace ttnx::playback {
namespace {
bool valid_dns_label(std::string_view s) {
    if (s.empty() || s.size() > 63 || s.front() == '-' ||
        s.back() == '-') return false;
    for (const unsigned char x : s) {
        if (!(x >= 'a' && x <= 'z') &&
            !(x >= '0' && x <= '9') && x != '-') return false;
    }
    return true;
}
bool reviewed_media_host(std::string_view hostname) {
    constexpr std::string_view suffix=".googlevideo.com";
    if (hostname.size() <= suffix.size() ||
        hostname.substr(hostname.size() - suffix.size()) != suffix) return false;
    auto host=hostname.substr(0,hostname.size()-suffix.size());
    while(!host.empty()) {
        const auto dot=host.find('.');
        const auto piece=host.substr(0,dot);
        if(!valid_dns_label(piece))return false;
        if(dot==std::string_view::npos)return true;
        host.remove_prefix(dot+1);
    }
    return false;
}
bool target_safe(std::string_view url) {
    if (url.size()>8192 || url.substr(0,8)!="https://")return false;
    for(const unsigned char x:url)if(x<0x21 || x>0x7e)return false;
    return true;
}
bool reviewed_hls_url(std::string_view url) {
    if(!target_safe(url))return false;
    const auto host_start=std::size_t{8};
    const auto slash=url.find('/',host_start);
    if(slash==std::string_view::npos)return false;
    const auto authority=url.substr(host_start,slash-host_start);
    if(authority.find('@')!=std::string_view::npos ||
       authority.find(':')!=std::string_view::npos ||
       authority.find('%')!=std::string_view::npos ||
       !reviewed_media_host(authority))return false;
    auto path=url.substr(slash);
    if(path.find('#')!=std::string_view::npos ||
       path.find('\\')!=std::string_view::npos ||
       path.find("//")!=std::string_view::npos)return false;
    const auto end=path.find('?');
    path=path.substr(0,end);
    // Live reference proved that visionOS can return a *media* playlist
    // through /hls_variant/ rather than the traditional /hls_playlist/
    // master. Admit only these two exact path families; this is still only
    // a URL-structure check, not permission to play arbitrary media.
    constexpr std::string_view master="/api/manifest/hls_playlist/";
    constexpr std::string_view variant="/api/manifest/hls_variant/";
    const bool reviewed=(path.starts_with(master) && path.size()>master.size()) ||
                        (path.starts_with(variant) && path.size()>variant.size());
    if(!reviewed)return false;
    std::string lower(path);
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return lower.find("/../")==std::string::npos &&
           lower.find("/./")==std::string::npos &&
           lower.find("%2e")==std::string::npos &&
           lower.find("%2f")==std::string::npos &&
           lower.find("%5c")==std::string::npos;
}
bool format_allowed(const SourceCandidate& c,unsigned height,unsigned fps) {
    return c.height!=0 && c.height<=height &&
           c.fps!=0 && c.fps<=fps && c.h264 && c.aac &&
           !c.drm && !c.requires_auth;
}
} // namespace

bool is_reviewable_hls_manifest_url(std::string_view url) {
    return reviewed_hls_url(url);
}

RouteResult choose_source_route(const VideoSourceSet& src,
                                unsigned max_height, unsigned max_fps) {
    RouteResult result;
    if(!core::is_valid_video_id(src.video_id)) {
        result.failure=SourceRouteFailure::InvalidId;return result;
    }
    if(src.is_short) {
        result.failure=SourceRouteFailure::ShortsForbidden;return result;
    }
    if(src.is_live || src.requires_auth) {
        result.failure=SourceRouteFailure::RestrictedOrDrm;return result;
    }
    if(!src.duration_ms || src.duration_ms>7ULL*24*3600*1000) {
        result.failure=SourceRouteFailure::InvalidDuration;return result;
    }
    max_height=std::min(max_height,1080U);
    max_fps=std::min(max_fps,60U);
    std::optional<PlannedSource> best;
    // Strictly bounded evaluation avoids unbounded work if a private
    // upstream endpoint returns an adversarially large format listing.
    for(std::size_t i=0;i<std::min(src.candidates.size(),std::size_t{200});++i) {
        const auto& s=src.candidates[i];
        if(!format_allowed(s,max_height,max_fps))continue;
        bool safe=false;
        int preference=0;
        switch(s.delivery) {
            case VideoDelivery::Hls:
                safe=s.audio_url.empty() && reviewed_hls_url(s.video_url);
                preference=3;break;
            case VideoDelivery::SeparateAv:
                safe=is_reviewable_googlevideo_https_url(s.video_url) &&
                     is_reviewable_googlevideo_https_url(s.audio_url) &&
                     s.video_url!=s.audio_url;
                preference=2;break;
            case VideoDelivery::ProgressiveMp4:
                safe=s.audio_url.empty() &&
                     is_reviewable_googlevideo_https_url(s.video_url);
                preference=1;break;
            default:break;
        }
        if(!safe)continue;
        const PlannedSource candidate{s.delivery,s.video_url,s.audio_url,
                                      s.height,s.fps,src.duration_ms};
        const auto score=[&](const PlannedSource& x) {
            int pref=x.delivery==VideoDelivery::Hls?3:
                     x.delivery==VideoDelivery::SeparateAv?2:1;
            return std::tuple(pref,x.height,x.fps);
        };
        if(!best || score(candidate)>score(*best)) best=candidate;
    }
    if(!best)return result;
    result.planned=std::move(best);
    result.failure=SourceRouteFailure::None;
    return result;
}

} // namespace ttnx::playback
