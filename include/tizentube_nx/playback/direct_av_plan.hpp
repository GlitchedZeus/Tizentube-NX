// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/core/content_filter.hpp"
#include <optional>
#include <string>

namespace ttnx::playback {
// Normal Search identity + independently approved direct player formats.
// Never infer Shorts status from an 11-character ID alone.
struct DirectAvPlan {
    std::string video_url, audio_url;
    std::uint64_t duration_ms{0};
    unsigned width{0},height{0},fps{0};
};
[[nodiscard]] inline std::optional<DirectAvPlan> admit_direct_av_plan(
    const ExtractionResult& extraction,
    const core::ContentItem& normal_search) {
    if(normal_search.kind!=core::ContentKind::Video ||
       core::should_hide(normal_search) ||
       !core::is_valid_video_id(normal_search.id) ||
       extraction.video_id!=normal_search.id ||
       extraction.is_short || extraction.is_live ||
       extraction.duration_ms<1000 ||
       extraction.duration_ms>12ULL*60*60*1000)return std::nullopt;
    QualityPolicy quality{};
    quality.max_height=720;
    quality.max_fps=60;
    quality.permit_vp9=false;
    quality.prefer_adaptive=true;
    const auto selected=select_streams(extraction,quality);
    if(!selected.streams)return std::nullopt;
    const auto& s=*selected.streams;
    if(!s.adaptive || s.video_codec!=VideoCodec::H264 ||
       s.audio_codec!=AudioCodec::Aac || s.video_url.empty() ||
       s.audio_url.empty() || s.video_url.size()>8192 ||
       s.audio_url.size()>8192 || s.height==0 || s.height>720 ||
       s.width==0 || s.width>1280 || s.fps==0 || s.fps>60 ||
       s.duration_ms!=extraction.duration_ms ||
       !is_reviewable_googlevideo_https_url(s.video_url) ||
       !is_reviewable_googlevideo_https_url(s.audio_url))
        return std::nullopt;
    return DirectAvPlan{s.video_url,s.audio_url,s.duration_ms,
                        s.width,s.height,s.fps};
}
} // namespace ttnx::playback
