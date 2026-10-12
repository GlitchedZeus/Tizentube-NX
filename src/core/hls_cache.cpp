#include "tizentube_nx/playback/hls_cache.hpp"
#include "tizentube_nx/playback/ts_streams.hpp"
#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/core/url.hpp"
#include <algorithm>

#include <utility>

namespace ttnx::playback {
namespace {
bool transient_http(int status) {
    return status==429 || status==500 || status==502 ||
           status==503 || status==504;
}
bool transient_segment(const HlsSegmentBytes& bytes) {
    return (bytes.failure==HlsSegmentFailure::Transport &&
            bytes.http_status==0) ||
           (bytes.failure==HlsSegmentFailure::HttpStatus &&
            transient_http(bytes.http_status));
}
bool transient_aac(const MediaReadResult& audio) {
    return (audio.failure==MediaReadFailure::TransportError &&
            audio.http_status==0) ||
           (audio.failure==MediaReadFailure::UnexpectedStatus &&
            transient_http(audio.http_status));
}
}
bool HlsCacheJob::start(const HlsProbePlan& plan) {
    cancel();
    active_.reset();
    bytes_=0;
    muxed_audio_verified_=false;
    video_codec_verified_=false;
    audio_codec_verified_=false;
    aac_offset_=0;
    aac_total_=0;
    aac_structure_.reset();
    startup_buffer_.reset();
    startup_event_staged_=false;
    startup_event_segments_=0;
    completed_=0;
    total_segments_=0;
    next_segment_=0;
    track_=0;
    paused_video_segment_=0;
    early_audio_complete_=false;
    transient_attempts_=0;
    retry_delay_ms_=0;
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
    std::vector<std::uint64_t> durations;
    durations.reserve(plan.video_segments.segments.size());
    for(const auto& item:plan.video_segments.segments)
        durations.push_back(item.duration_ms);
    if(!startup_buffer_.configure(durations))return false;
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
    retry_delay_ms_=0;
    const auto temporary_failure=[this]() {
        if(transient_attempts_>=kMaxTransientRetries)return false;
        ++transient_attempts_;
        retry_delay_ms_=250*transient_attempts_;
        return true;
    };
    const auto request_succeeded=[this]() {
        transient_attempts_=0;
        retry_delay_ms_=0;
    };

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
            startup_buffer_.mark_complete_audio(true);
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
        if(!audio.ok() || audio.bytes.empty()) {
            if(transient_aac(audio) && temporary_failure())return state_;
            return failed(HlsCacheFailure::MediaFetch);
        }
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
        request_succeeded();
        if(aac_offset_>aac_total_)
            return failed(HlsCacheFailure::MediaFetch);
        return state_;
    }

    const auto& playlist=track_==0
        ?active_->video_segments:*active_->audio_segments;
    if(next_segment_>=playlist.segments.size() && !init_pending_) {
        if(track_==1) {
            // Audio playlist has completed successfully, but this alone does
            // not authorize libmpv: we still require every video segment and
            // final local manifest transaction before playback.
            early_audio_complete_=true;
            // MPEG-TS audio needs verified AAC program metadata; fMP4
            // renditions still rely on the final libmpv codec gate, never
            // advertise early-playback readiness without independent proof.
            if(audio_codec_verified_)startup_buffer_.mark_complete_audio(true);
            track_=0;
            next_segment_=paused_video_segment_;
            init_pending_=false;
            return state_;
        }
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
        if(track_==0 && active_->audio_segments &&
           !early_audio_complete_) {
            // Defensive fallback for any future alternate transfer order.
            paused_video_segment_=next_segment_;
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
        if(transient_segment(bytes) && temporary_failure())return state_;
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
    if(track_==1 && !is_init &&
       !active_->audio_segments->init_segment_url) {
        // The first TS audio segment must actually advertise AAC, not just
        // return HTTP 200 with a plausible video/mp2t MIME type.
        const auto streams=inspect_mpeg_ts(bytes.bytes);
        if(next_segment_==0 && !(streams.recognized && streams.aac)) {
            sink_.abort();
            state_=HlsCacheState::Failed;
            error_=HlsCacheFailure::MissingAudio;
            active_.reset();
            return state_;
        }
        if(streams.recognized && streams.aac)audio_codec_verified_=true;
    }
    if(track_==0 && !is_init &&
       !active_->video_segments.init_segment_url) {
        const auto streams=inspect_mpeg_ts(bytes.bytes);
        if(streams.recognized && streams.h264) {
            video_codec_verified_=true;
            startup_buffer_.mark_video_codec(true);
            if(streams.aac) {
                muxed_audio_verified_=true;
                startup_buffer_.mark_complete_audio(true);
            }
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
    request_succeeded();
    if(is_init)init_pending_=false;
    else {
        if(track_==0) {
            // Only valid TS codec and fully verified audio can unlock
            // future startup buffering. fMP4 remains conservative.
            (void)startup_buffer_.add_video(
                next_segment_,playlist.segments[next_segment_].duration_ms,
                bytes.bytes.size());
        }
        ++next_segment_;
        ++completed_;
    }
    // A completed AAC and verified video prefix may create a private,
    // never-user-opened EVENT snapshot in pending/. Do not interpret
    // this as permission to switch the active player from ready/.
    if(track_==0 && !is_init && startup_buffer_.prepared() &&
       next_segment_>startup_event_segments_) {
        // The sink only snapshots complete fsync'd segment files; this
        // metadata stays in pending/ and is not opened by current libmpv.
        if(sink_.stage_startup_event(*active_,next_segment_)) {
            startup_event_staged_=true;
            startup_event_segments_=next_segment_;
        }
    }
    // For a separate HLS audio rendition, probe AAC after the first real
    // video segment, then resume the remaining H.264 segments afterward.
    // A 403, wrong type, or broken audio rendition no longer wastes the
    // entire video download. Never offer this partial state to libmpv.
    if(track_==0 && !is_init && next_segment_==1 &&
       active_->audio_segments && !early_audio_complete_) {
        if(!active_->video_segments.init_segment_url &&
           !video_codec_verified_) {
            sink_.abort();
            state_=HlsCacheState::Failed;
            error_=HlsCacheFailure::UnsupportedVideo;
            active_.reset();
            return state_;
        }
        paused_video_segment_=next_segment_;
        track_=1;
        next_segment_=0;
        init_pending_=active_->audio_segments->init_segment_url.has_value();
        return state_;
    }
    // Direct TS with neither external nor muxed AAC cannot be played.
    // Fail after the first verified segment, not the entire silent VOD.
    if(track_==0 && !is_init && next_segment_==1 &&
       !active_->audio_segments &&
       active_->source.external_aac_url.empty() &&
       !active_->video_segments.init_segment_url &&
       (!video_codec_verified_ || !muxed_audio_verified_)) {
        sink_.abort();
        state_=HlsCacheState::Failed;
        error_=video_codec_verified_
            ?HlsCacheFailure::MissingAudio:HlsCacheFailure::UnsupportedVideo;
        active_.reset();
        return state_;
    }
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
    startup_buffer_.reset();
    startup_event_staged_=false;
    startup_event_segments_=0;
    paused_video_segment_=0;
    early_audio_complete_=false;
    transient_attempts_=0;
    retry_delay_ms_=0;
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
