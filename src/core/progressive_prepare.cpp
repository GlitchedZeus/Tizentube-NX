#include "tizentube_nx/playback/progressive_prepare.hpp"

#include "tizentube_nx/core/url.hpp"

#include <utility>

namespace ttnx::playback {

bool ProgressivePrepareJob::start(std::string_view normal_video_id) {
    cancel();
    error_=ProgressiveError::None;
    video_id_.clear();
    duration_ms_=0;
    if (!ttnx::core::is_valid_video_id(normal_video_id)) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::InvalidId;
        return false;
    }

    // This method performs an extractor HTTP request. Only call on a
    // cancellable background worker after a Search normal-video selection.
    const auto resolved=resolver_.resolve(normal_video_id);
    if (!resolved) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::ResolverUnavailable;
        return false;
    }
    if (resolved->video_id != normal_video_id) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::IdentityMismatch;
        return false;
    }
    if (resolved->is_short) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::ShortsForbidden;
        return false;
    }
    if (resolved->is_live) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::NoSafeMuxedMp4;
        return false;
    }

    // The physical Switch has already accepted local muxed H264/AAC playback.
    // This first real-video gate deliberately does not pretend a separate
    // audio-only stream can be attached by the current offline player.
    // Prefer the best H264/AAC muxed MP4 no higher than 720p30; native
    // adaptive-stream synchronization belongs in a later hardware gate.
    ExtractionResult muxed=*resolved;
    muxed.audio.clear();
    for (auto& format:muxed.video) {
        if (format.muxed_audio != AudioCodec::Aac)
            format.codec=VideoCodec::Unknown;
    }
    QualityPolicy policy;
    policy.max_height=720;
    policy.max_fps=30;
    policy.permit_vp9=false;
    policy.prefer_adaptive=false;
    const auto streams=select_streams(muxed,policy);
    if (!streams.streams || streams.streams->adaptive ||
        streams.streams->video_codec!=VideoCodec::H264 ||
        streams.streams->audio_codec!=AudioCodec::Aac) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::NoSafeMuxedMp4;
        return false;
    }
    if (!downloader_.start(streams.streams->video_url)) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::CacheUnavailable;
        return false;
    }
    video_id_=std::string(normal_video_id);
    duration_ms_=streams.streams->duration_ms;
    stage_=ProgressiveStage::Downloading;
    return true;
}

ProgressiveStage ProgressivePrepareJob::pump() {
    if (stage_ != ProgressiveStage::Downloading) return stage_;
    const auto result=downloader_.pump();
    if (result == MediaDownloadState::Complete) {
        stage_=ProgressiveStage::Ready;
    } else if (result == MediaDownloadState::Error ||
               result == MediaDownloadState::Cancelled) {
        stage_=ProgressiveStage::Error;
        error_=ProgressiveError::DownloadFailed;
    }
    return stage_;
}

void ProgressivePrepareJob::cancel() noexcept {
    downloader_.cancel();
    if (stage_==ProgressiveStage::Downloading)
        stage_=ProgressiveStage::Cancelled;
    video_id_.clear();
    duration_ms_=0;
}

} // namespace ttnx::playback
