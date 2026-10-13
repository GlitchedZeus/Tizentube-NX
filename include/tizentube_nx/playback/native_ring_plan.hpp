// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tizentube_nx/playback/hls_probe.hpp"
#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/url.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::playback {
// Strict native M4 transport admission independent of Switch UI and sockets.
// No ring, HTTP client, file, signed URL logging or decoder is created here.
// Returned IDs/URLs remain in worker memory only, never persistent storage.
struct NativeRingPlan {
    std::vector<std::string> urls;
    std::vector<std::uint64_t> durations_ms;
    std::uint64_t total_duration_ms{0};
};
[[nodiscard]] inline std::optional<NativeRingPlan> admit_native_ring_plan(
    const HlsProbePlan& plan,const core::ContentItem& normal_browse_result) {
    // A syntactically valid ID alone is NOT evidence of normal Search origin.
    if(normal_browse_result.kind!=core::ContentKind::Video ||
       core::should_hide(normal_browse_result) ||
       normal_browse_result.id!=plan.source.video_id ||
       !core::is_valid_video_id(plan.source.video_id))return std::nullopt;
    // The ring currently supports only muxed finite H264/AAC MPEG-TS.
    // Separate audio, live, fMP4 init chunks and unknown codecs fail closed.
    if(!plan.selected.height || plan.selected.height>720 ||
       !plan.selected.width || plan.selected.width>1280 ||
       !plan.selected.fps || plan.selected.fps>60 ||
       plan.selected.direct_media_playlist ||
       plan.audio_segments.has_value() ||
       !plan.selected.audio_playlist_url.empty() ||
       !plan.source.external_aac_url.empty() ||
       plan.video_segments.init_segment_url.has_value() ||
       plan.video_segments.segments.empty() ||
       plan.video_segments.segments.size()>4096 ||
       !plan.source.duration_ms ||
       plan.source.duration_ms>12ULL*60*60*1000)return std::nullopt;
    NativeRingPlan accepted;
    accepted.urls.reserve(plan.video_segments.segments.size());
    accepted.durations_ms.reserve(plan.video_segments.segments.size());
    std::size_t url_bytes=0;
    for(const auto& segment:plan.video_segments.segments) {
        if(!segment.duration_ms || segment.duration_ms>60000 ||
           accepted.total_duration_ms>12ULL*60*60*1000-segment.duration_ms ||
           segment.url.empty() || segment.url.size()>4096 ||
           url_bytes>512*1024-segment.url.size() ||
           !is_reviewable_hls_child_url(segment.url))return std::nullopt;
        accepted.total_duration_ms+=segment.duration_ms;
        url_bytes+=segment.url.size();
        accepted.urls.push_back(segment.url);
        accepted.durations_ms.push_back(segment.duration_ms);
    }
    // Do not reinterpret inconsistent upstream source/playlist timing.
    if(plan.video_segments.total_duration_ms!=accepted.total_duration_ms)
        return std::nullopt;
    const auto mismatch=accepted.total_duration_ms>plan.source.duration_ms
        ?accepted.total_duration_ms-plan.source.duration_ms
        :plan.source.duration_ms-accepted.total_duration_ms;
    return mismatch<=3000?std::optional<NativeRingPlan>(std::move(accepted))
                         :std::nullopt;
}
} // namespace ttnx::playback
