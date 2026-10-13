#include "tizentube_nx/playback/hls_segment.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace ttnx::playback {
namespace {
bool equal_ci(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        if(std::tolower(static_cast<unsigned char>(a[i])) !=
           std::tolower(static_cast<unsigned char>(b[i])))return false;
    }
    return true;
}
bool media_type(std::string_view value) {
    const auto semicolon=value.find(';');
    value=value.substr(0,semicolon);
    while(!value.empty() && value.back()==' ')value.remove_suffix(1);
    return value=="video/mp4" || value=="video/mp2t" ||
           value=="audio/mp4" || value=="audio/aac" ||
           value=="application/octet-stream" ||
           value=="binary/octet-stream";
}
} // namespace

HlsSegmentBytes HlsSegmentReader::fetch(std::string_view url) const {
    HlsSegmentBytes out;
    if(!is_reviewable_hls_child_url(url))return out;
    net::HttpRequest request;
    request.method=net::HttpMethod::Get;
    request.url=std::string(url);
    request.timeout_ms=20000;
    request.headers={
        {"Accept","application/octet-stream"},
        {"Accept-Encoding","identity"},
    };
    const auto response=http_.perform(request);
    out.http_status=response.response.status_code;
    if(!response.ok) {
        // 403 received through a !ok transport result is still a
        // permanent HTTP refusal, not a transient socket failure.
        out.failure=out.http_status>=400
            ?HlsSegmentFailure::HttpStatus:HlsSegmentFailure::Transport;
        return out;
    }
    if(response.response.status_code!=200) {
        out.failure=HlsSegmentFailure::HttpStatus;
        return out;
    }
    const auto& bytes=response.response.body;
    if(bytes.size()<12 || bytes.size()>kMaximumSegmentBytes) {
        out.failure=HlsSegmentFailure::InvalidLength;
        return out;
    }
    unsigned type_count=0,length_count=0,encoding_count=0;
    for(const auto& h:response.response.headers) {
        if(equal_ci(h.name,"Content-Type")) {
            ++type_count;
            if(!media_type(h.value)) {
                out.failure=HlsSegmentFailure::WrongType;
                return out;
            }
        }
        if(equal_ci(h.name,"Content-Encoding")) {
            ++encoding_count;
            if(!equal_ci(h.value,"identity")) {
                out.failure=HlsSegmentFailure::CompressedOrAmbiguous;
                return out;
            }
        }
        if(equal_ci(h.name,"Content-Length")) {
            ++length_count;
            std::uint64_t value=0;
            if(h.value.empty() || h.value.size()>9) {
                out.failure=HlsSegmentFailure::InvalidLength;
                return out;
            }
            for(const unsigned char c:h.value) {
                if(c<'0'||c>'9') {
                    out.failure=HlsSegmentFailure::InvalidLength;
                    return out;
                }
                value=value*10+(c-'0');
            }
            if(value!=bytes.size()) {
                out.failure=HlsSegmentFailure::InvalidLength;
                return out;
            }
        }
    }
    if(type_count!=1 || length_count>1 || encoding_count>1) {
        out.failure=HlsSegmentFailure::CompressedOrAmbiguous;
        return out;
    }
    // Deny obvious error documents. Codec/container validation is a separate
    // demux/decoder gate; this check is not proof the data is playable.
    if(std::string_view(bytes).starts_with("<!DOCTYPE") ||
       std::string_view(bytes).starts_with("<html") ||
       std::string_view(bytes).starts_with("#EXTM3U")) {
        out.failure=HlsSegmentFailure::NotMedia;
        return out;
    }
    out.bytes=bytes;
    out.failure=HlsSegmentFailure::None;
    return out;
}
HlsStreamResult HlsSegmentReader::stream(std::string_view url,
                                          const HlsMediaChunk& emit) const {
    if(!emit || !is_reviewable_hls_child_url(url))return {};
    if(stream_)return stream_->stream_segment(url,emit);
#if defined(__SWITCH__) && defined(TTNX_M4_ROLLING_STREAM)
    // Never fall back to a 16-MiB whole-response allocation on real
    // Switch M4 builds. The native streaming adapter must be injected.
    return {};
#endif
    // Fake HttpClient compatibility for *host regressions only*: the
    // existing fetch() still buffers the complete response. Native rolling
    // sessions must inject the approved HlsSegmentStreamTransport.
    const auto response=fetch(url);
    if(!response.ok())return {};
    const std::string_view all=response.bytes;
    for(std::size_t at=0;at<all.size();at+=32*1024) {
        if(!emit(all.substr(at,std::min<std::size_t>(32*1024,all.size()-at))))
            return {};
    }
    return {true,all.size()};
}

} // namespace ttnx::playback
