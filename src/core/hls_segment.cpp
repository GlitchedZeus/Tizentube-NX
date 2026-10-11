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
    if(!response.ok) {
        out.failure=HlsSegmentFailure::Transport;
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
} // namespace ttnx::playback
