#include "tizentube_nx/playback/hls_probe.hpp"
#include "tizentube_nx/playback/source_routes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <string>
#include <thread>

namespace ttnx::playback {
namespace {
bool header_equal(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        const auto x=static_cast<unsigned char>(a[i]);
        const auto y=static_cast<unsigned char>(b[i]);
        if(std::tolower(x)!=std::tolower(y))return false;
    }
    return true;
}
bool valid_mime(std::string_view mime) {
    const auto end=mime.find(';');
    mime=mime.substr(0,end);
    while(!mime.empty() && mime.back()==' ')mime.remove_suffix(1);
    return mime=="application/vnd.apple.mpegurl" ||
           mime=="application/x-mpegurl" ||
           mime=="audio/mpegurl" ||
           mime=="application/octet-stream";
}
bool within_sync_window(std::uint64_t a,std::uint64_t b) {
    if(!a || !b)return false;
    const auto gap=a>b?a-b:b-a;
    return gap<=std::max<std::uint64_t>(3000,std::max(a,b)/100);
}
} // namespace

std::optional<std::string> HlsSourceProbe::fetch_playlist(
    std::string_view url, bool master) {
    if (url.size()>8192 ||
        (master
          ? !is_reviewable_hls_manifest_url(url)
          : !is_reviewable_hls_child_url(url))) {
        last_error_=HlsProbeError::ManifestRejected;
        return std::nullopt;
    }
    net::HttpRequest request;
    request.method=net::HttpMethod::Get;
    request.url=std::string(url);
    request.timeout_ms=15000;
    request.headers={
        {"Accept","application/vnd.apple.mpegurl"},
        {"Accept-Encoding","identity"},
    };
    net::HttpResult response;
    for(unsigned attempt=0;attempt<3;++attempt) {
        response=media_http_.perform(request);
        const int status=response.response.status_code;
        if(response.ok && status==200)break;
        const bool temporary=(!response.ok && status==0) ||
            status==429 || status==500 || status==502 ||
            status==503 || status==504;
        if(temporary && attempt<2) {
            // Source probing is worker-only. Delay each request rather than
            // hammering a throttled signed playlist endpoint or blocking UI.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(250*(attempt+1)));
            continue;
        }
        // Preserve the distinction between HTTP refusal and an absent
        // connection, without ever reflecting bearer URLs in diagnostics.
        last_error_=status?HlsProbeError::ManifestStatus:
                           HlsProbeError::ManifestTransport;
        return std::nullopt;
    }
    if(response.response.body.empty() ||
       response.response.body.size()>1024*1024) {
        last_error_=HlsProbeError::ManifestRejected;
        return std::nullopt;
    }
    unsigned mime_count=0,length_count=0,encoding_count=0;
    for(const auto& h:response.response.headers) {
        if(header_equal(h.name,"Content-Type")) {
            ++mime_count;
            if(!valid_mime(h.value)) {
                last_error_=HlsProbeError::ManifestRejected;
                return std::nullopt;
            }
        } else if(header_equal(h.name,"Content-Encoding")) {
            ++encoding_count;
            if(!header_equal(h.value,"identity")) {
                last_error_=HlsProbeError::ManifestRejected;
                return std::nullopt;
            }
        } else if(header_equal(h.name,"Content-Length")) {
            ++length_count;
            std::uint64_t value=0;
            if(h.value.empty() || h.value.size()>7) {
                last_error_=HlsProbeError::ManifestRejected;
                return std::nullopt;
            }
            for(unsigned char c:h.value) {
                if(c<'0'||c>'9') {
                    last_error_=HlsProbeError::ManifestRejected;
                    return std::nullopt;
                }
                value=value*10+static_cast<unsigned>(c-'0');
            }
            if(value!=response.response.body.size()) {
                last_error_=HlsProbeError::ManifestRejected;
                return std::nullopt;
            }
        }
    }
    if(mime_count!=1 || length_count>1 || encoding_count>1) {
        last_error_=HlsProbeError::ManifestRejected;
        return std::nullopt;
    }
    if(!std::string_view(response.response.body).starts_with("#EXTM3U")) {
        last_error_=HlsProbeError::ManifestRejected;
        return std::nullopt;
    }
    return response.response.body;
}

std::optional<HlsProbePlan> HlsSourceProbe::inspect(
    std::string_view normal_video_id,unsigned max_height,unsigned max_fps) {
    last_error_=HlsProbeError::SourceUnavailable;
    VisionHlsSource source_reader(guest_http_,guest_);
    const auto source=source_reader.resolve(normal_video_id);
    if(!source) {
        switch(source_reader.last_error()) {
            case HlsSourceError::Transport:
                last_error_=HlsProbeError::GuestTransport;break;
            case HlsSourceError::HttpStatus:
                last_error_=HlsProbeError::GuestHttpStatus;break;
            case HlsSourceError::ResponseRejected:
                last_error_=HlsProbeError::GuestPlayabilityRejected;break;
            default:break;
        }
        return std::nullopt;
    }

    const auto master_bytes=fetch_playlist(source->master_playlist_url,true);
    if(!master_bytes)return std::nullopt;
    std::optional<HlsVariant> variant;
    std::optional<HlsMediaPlaylist> video;
    // The LIVE reference accepted a direct hls_variant VOD playlist.
    // It has EXTINF entries but no master STREAM-INF entries. Do not
    // erroneously try to parse/fetch a master, or silently add an audio
    // rendition absent from the actual source metadata.
    const std::string_view manifest(*master_bytes);
    const bool contains_master=manifest.find("#EXT-X-STREAM-INF:")!=
                               std::string_view::npos;
    const bool contains_media=manifest.find("#EXTINF:")!=
                              std::string_view::npos;
    if(contains_master && !contains_media) {
        const auto master=parse_hls_master_playlist(
            manifest,source->master_playlist_url);
        if(!master) {
            last_error_=HlsProbeError::ManifestRejected;
            return std::nullopt;
        }
        variant=choose_hls_variant(*master,max_height,max_fps);
        if(!variant) {
            last_error_=HlsProbeError::VariantUnavailable;
            return std::nullopt;
        }
        const auto video_data=fetch_playlist(variant->playlist_url,false);
        if(!video_data)return std::nullopt;
        video=parse_hls_media_playlist(*video_data,variant->playlist_url);
    } else if(contains_media && !contains_master) {
        video=parse_hls_media_playlist(manifest,source->master_playlist_url);
        if(video) {
            HlsVariant direct;
            direct.playlist_url=source->master_playlist_url;
            direct.direct_media_playlist=true;
            variant=std::move(direct);
        }
    }
    if(!variant || !video) {
        last_error_=HlsProbeError::VideoSegmentsRejected;
        return std::nullopt;
    }

    std::optional<HlsMediaPlaylist> audio;
    if(!variant->audio_playlist_url.empty()) {
        const auto bytes=fetch_playlist(variant->audio_playlist_url,false);
        if(!bytes)return std::nullopt;
        audio=parse_hls_media_playlist(*bytes,variant->audio_playlist_url);
        if(!audio) {
            last_error_=HlsProbeError::AudioSegmentsRejected;
            return std::nullopt;
        }
        if(!within_sync_window(video->total_duration_ms,
                               audio->total_duration_ms)) {
            last_error_=HlsProbeError::InvalidDuration;
            return std::nullopt;
        }
    }
    // An upstream playback response and playlist that disagree by minutes
    // should not be treated as a verified playable single normal video.
    if(!within_sync_window(video->total_duration_ms,source->duration_ms)) {
        last_error_=HlsProbeError::InvalidDuration;
        return std::nullopt;
    }
    last_error_=HlsProbeError::None;
    return HlsProbePlan{*source,*variant,std::move(*video),std::move(audio)};
}

const char* describe_hls_probe_failure(HlsProbeError reason) noexcept {
    switch(reason) {
        case HlsProbeError::None:return "Ready to download video and audio.";
        case HlsProbeError::SourceUnavailable:return "Guest video source unavailable.";
        case HlsProbeError::GuestTransport:return "YouTube connection failed. Retry from Search.";
        case HlsProbeError::GuestHttpStatus:return "YouTube rejected anonymous playback.";
        case HlsProbeError::GuestPlayabilityRejected:return "This video cannot be played as a guest.";
        case HlsProbeError::ManifestTransport:return "Media host could not be reached.";
        case HlsProbeError::ManifestStatus:return "Signed video playlist expired or was refused.";
        case HlsProbeError::ManifestRejected:return "The video playlist is unsupported or unsafe.";
        case HlsProbeError::VariantUnavailable:return "No Switch-compatible H.264 video was provided.";
        case HlsProbeError::VideoSegmentsRejected:return "Video segments are unsupported or unsafe.";
        case HlsProbeError::AudioSegmentsRejected:return "AAC audio segments are unsupported or unsafe.";
        case HlsProbeError::InvalidDuration:return "Video and audio duration mismatch.";
    }
    return "Video source unavailable.";
}
} // namespace ttnx::playback
