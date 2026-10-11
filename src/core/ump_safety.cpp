#include "tizentube_nx/playback/ump_wire.hpp"
#include "tizentube_nx/playback/playback.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace ttnx::playback {
namespace {
constexpr std::size_t kMaxMediaChunk=1024*1024;
constexpr std::size_t kMaxEnvelope=kMaxMediaChunk+128*1024;
constexpr std::uint64_t kMaxResource=512ULL*1024*1024;
}

std::optional<std::vector<uint8_t>> parse_bounded_ump_media(
    const uint8_t* bytes, std::size_t response_size) {
    if(!bytes || response_size<3 || response_size>kMaxEnvelope)
        return std::nullopt;
    const auto parsed=parse_ump_response(bytes,response_size);
    if(!parsed.complete || parsed.media_data.empty() ||
       parsed.media_data.size()>kMaxMediaChunk ||
       !parsed.redirect_url.empty() ||
       !parsed.stream_protection_statuses.empty())
        return std::nullopt;
    // A redirect/protection envelope is not a media buffer. This worker will
    // not automatically follow it; a reviewed caller may validate and issue
    // a *new* request in a separate future milestone.
    return parsed.media_data;
}

std::optional<std::string> reviewed_ump_chunk_url(
    const std::string& signed_video_url,std::uint64_t offset,
    std::uint64_t known_resource_size,int request_number) {
    if(!is_reviewable_googlevideo_https_url(signed_video_url) ||
       known_resource_size==0 || known_resource_size>kMaxResource ||
       offset>=known_resource_size || request_number<1 ||
       request_number>4096) return std::nullopt;
    const auto max_end=std::min(known_resource_size-1,
                                offset+kMaxMediaChunk-1);
    if(max_end<offset) return std::nullopt;
    const auto request=build_ump_request_url(signed_video_url,offset,
                                              max_end,request_number);
    if(request.size()>8192 ||
       !is_reviewable_googlevideo_https_url(request))
        return std::nullopt;
    return request;
}
} // namespace ttnx::playback
