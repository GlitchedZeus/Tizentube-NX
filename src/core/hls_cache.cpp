#include "tizentube_nx/playback/hls_cache.hpp"
#include "tizentube_nx/playback/ts_streams.hpp"
#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/core/url.hpp"
#include <algorithm>

#include <utility>

namespace ttnx::playback {
bool HlsCacheJob::start(const HlsProbePlan& plan) {
    cancel();
    active_.reset();
    bytes_=0;
    muxed_audio_verified_=false;
    video_codec_verified_=false;
    aac_offset_=0;
    aac_total_=0;
    aac_structure_.reset();
    completed_=0;
    total_segments_=0;
    next_segment_=0;
    track_=0;
    error_=HlsCacheFailure::InvalidPlaylist;
    state_=HlsCacheState::Failed;
    const auto count=plan.video_segments.segments.size()+
        (plan.audio_segments?plan.audio_segments->segments.size():0);
    // Recheck media identity/duration at the final transfer boundary, not
    // only in the upstream probe. Reject malformed plans before any I/O.
    const auto within_sync=[](std::uint64_t a,std::uint64_t b) {
        if(!a || !b)return false;
        const auto gap=a>b?a-b:b-a;
        return gap<=std::max<std::uint64_t>(3000,std::max(a,b)/100);
    };
    if(!core::is_valid_video_id(plan.source.video_id) ||
       !within_sync(plan.video_segments.total_duration_ms,
                    plan.source.duration_ms) ||
       (plan.audio_segments &&
        !within_sync(plan.audio_segments->total_duration_ms,
                     plan.video_segments.total_duration_ms)))
        return false;
    if(!plan.audio_segments && !plan.source.external_aac_url.empty() &&
       (!aac_reader_ ||
        !is_reviewable_googlevideo_https_url(plan.source.external_aac_url))) {
        error_=HlsCacheFailure::MissingAudio;
        return false;
    }
    if(plan.video_segments.segments.empty() || count>4096 ||
       ((!plan.selected.direct_media_playlist) &&
          (!plan.selected.height || !plan.selected.fps)) ||
       plan.video_segments.total_duration_ms==0 ||
       (plan.audio_segments && plan.audio_segments->segments.empty()) ||
       (!plan.selected.audio_playlist_url.empty() && !plan.audio_segments) ||
       (plan.selected.audio_playlist_url.empty() && plan.audio_segments))
        return false;
    if(!sink_.begin(plan)) {
        error_=HlsCacheFailure::UnableToStage;
        sink_.abort();
        return false;
    }
    total_segments_=count;
    active_=plan;
    track_=0;
    next_segment_=0;
    init_pending_=plan.video_segments.init_segment_url.has_value();
    state_=HlsCacheState::Fetching;
    error_=HlsCacheFailure::None;
    return true;
}

HlsCacheState HlsCacheJob::pump() {
    if(state_!=HlsCacheState::Fetching || !active_)return state_;

    // Standalone AAC from the SAME validated visionOS player response is
    // transferred through the already reviewed bounded CDN Range client.
    // No libmpv socket or signed URL is ever involved.
    if(track_==2) {
        auto failed=[this](HlsCacheFailure reason) {
            sink_.abort();state_=HlsCacheState::Failed;error_=reason;
            active_.reset();return state_;
        };
        if(aac_total_ && aac_offset_==aac_total_) {
            if(!aac_structure_.complete())
                return failed(HlsCacheFailure::MissingAudio);
            // The audio is ready, but no public cache exists until ALL
            // remaining video segments have been fetched and validated.
            track_=0;
            return state_;
        }
        if(!aac_reader_ || active_->source.external_aac_url.empty())
            return failed(HlsCacheFailure::MissingAudio);
        const auto range=next_media_range(aac_offset_,aac_total_);
        if(!range)return failed(HlsCacheFailure::MediaFetch);
        const auto audio=aac_reader_->read(active_->source.external_aac_url,*range);
        if(!audio.ok() || audio.bytes.empty())
            return failed(HlsCacheFailure::MediaFetch);
        if(audio.resource_size==0 || audio.resource_size>kMaxAacBytes ||
           (aac_total_ && audio.resource_size!=aac_total_) ||
           audio.bytes.size()>kMaxTotalBytes-bytes_)
            return failed(HlsCacheFailure::TotalSizeExceeded);
        if(aac_offset_==0) {
            if(audio.bytes.size()<12 ||
               std::string_view(audio.bytes).substr(4,4)!="ftyp")
                return failed(HlsCacheFailure::MissingAudio);
            aac_total_=audio.resource_size;
            if(!aac_structure_.begin(aac_total_))
                return failed(HlsCacheFailure::MissingAudio);
        }
        if(!aac_structure_.append(audio.bytes))
            return failed(HlsCacheFailure::MissingAudio);
        if(!sink_.append_external_aac(audio.bytes))
            return failed(HlsCacheFailure::MediaWrite);
        aac_offset_+=audio.bytes.size();
        bytes_+=audio.bytes.size();
        if(aac_offset_>aac_total_)
            return failed(HlsCacheFailure::MediaFetch);
        return state_;
    }

    const auto& playlist=track_==0
        ?active_->video_segments:*active_->audio_segments;
    if(next_segment_>=playlist.segments.size() && !init_pending_) {
        // Direct TS manifests do not advertise a trustworthy codec string.
        // Never package a random 200-OK response as playable H.264.
        if(track_==0 && !active_->video_segments.init_segment_url &&
           !video_codec_verified_) {
            sink_.abort();
            state_=HlsCacheState::Failed;
            error_=HlsCacheFailure::UnsupportedVideo;
            active_.reset();
            return state_;
        }
        if(track_==0 && !active_->audio_segments) {
            if(!active_->source.external_aac_url.empty()) {
                if(!aac_total_ || aac_offset_!=aac_total_) {
                    sink_.abort();
                    state_=HlsCacheState::Failed;
                    error_=HlsCacheFailure::MissingAudio;
                    active_.reset();
                    return state_;
                }
            } else if(!muxed_audio_verified_) {
                sink_.abort();
                state_=HlsCacheState::Failed;
                error_=HlsCacheFailure::MissingAudio;
                active_.reset();
                return state_;
            }
        }
        if(track_==0 && active_->audio_segments) {
            track_=1;
            next_segment_=0;
            init_pending_=active_->audio_segments->init_segment_url.has_value();
            return state_;
        }
        if(!sink_.publish(*active_)) {
            sink_.abort();
            state_=HlsCacheState::Failed;
            error_=HlsCacheFailure::UnableToPublish;
        } else {
            state_=HlsCacheState::Ready;
        }
        active_.reset(); // release all ephemeral signed media bearer URLs
        return state_;
    }
    const bool is_init=init_pending_;
    const std::string& url=is_init?*playlist.init_segment_url:
                                     playlist.segments[next_segment_].url;
    const auto bytes=reader_.fetch(url);
    if(!bytes.ok()) {
        sink_.abort();
        state_=HlsCacheState::Failed;
        error_=HlsCacheFailure::MediaFetch;
        active_.reset();
        return state_;
    }
    if(bytes.bytes.size()>kMaxTotalBytes-bytes_) {
        sink_.abort();
        state_=HlsCacheState::Failed;
        error_=HlsCacheFailure::TotalSizeExceeded;
        active_.reset();
        return state_;
    }
    if(track_==0 && !is_init &&
       !active_->video_segments.init_segment_url) {
        const auto streams=inspect_mpeg_ts(bytes.bytes);
        if(streams.recognized && streams.h264) {
            video_codec_verified_=true;
            if(streams.aac)muxed_audio_verified_=true;
        }
        // PAT/PMT recognition is only an early codec gate, never a claim
        // that actual H.264 frames or AAC samples decode successfully.
    }
    if(!sink_.write_segment(track_==1,is_init,next_segment_,bytes.bytes)) {
        sink_.abort();
        state_=HlsCacheState::Failed;
        error_=HlsCacheFailure::MediaWrite;
        active_.reset();
        return state_;
    }
    bytes_+=bytes.bytes.size();
    if(is_init)init_pending_=false;
    else {++next_segment_;++completed_;}
    // Validate one real video segment first. If a separately signed AAC
    // rendition is needed, fetch it before downloading the ENTIRE HLS VOD.
    // A 403/expired AAC thus aborts after at most the video init + first
    // segment, not after minutes of useless video and SD writes.
    if(track_==0 && !is_init && next_segment_==1 &&
       !active_->audio_segments && aac_total_==0 &&
       !active_->source.external_aac_url.empty()) {
        if(!active_->video_segments.init_segment_url && !video_codec_verified_) {
            sink_.abort();
            state_=HlsCacheState::Failed;
            error_=HlsCacheFailure::UnsupportedVideo;
            active_.reset();
            return state_;
        }
        track_=2;
    }
    return state_;
}

void HlsCacheJob::cancel() noexcept {
    if(state_==HlsCacheState::Fetching) {
        sink_.abort();
        state_=HlsCacheState::Cancelled;
    }
    active_.reset();
}
const char* describe_hls_cache_failure(HlsCacheFailure reason) noexcept {
    switch(reason) {
        case HlsCacheFailure::None:return "Both media tracks are ready.";
        case HlsCacheFailure::InvalidPlaylist:return "Video source failed final validation.";
        case HlsCacheFailure::UnableToStage:return "Unable to stage media on the SD card.";
        case HlsCacheFailure::MediaFetch:return "Media download failed or expired. Retry.";
        case HlsCacheFailure::MediaWrite:return "SD media write failed. Check space.";
        case HlsCacheFailure::TotalSizeExceeded:return "Video exceeds the safe offline cache limit.";
        case HlsCacheFailure::UnableToPublish:return "Completed media cache could not be published.";
        case HlsCacheFailure::MissingAudio:return "No supported AAC audio was available.";
        case HlsCacheFailure::UnsupportedVideo:return "No verified H.264 video track was found.";
    }
    return "Media preparation failed safely.";
}
} // namespace ttnx::playback
