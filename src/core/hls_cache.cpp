#include "tizentube_nx/playback/hls_cache.hpp"
#include "tizentube_nx/playback/ts_streams.hpp"

#include <utility>

namespace ttnx::playback {
bool HlsCacheJob::start(const HlsProbePlan& plan) {
    cancel();
    active_.reset();
    bytes_=0;
    muxed_audio_verified_=false;
    aac_offset_=0;
    aac_total_=0;
    completed_=0;
    next_segment_=0;
    track_=0;
    error_=HlsCacheFailure::InvalidPlaylist;
    state_=HlsCacheState::Failed;
    const auto count=plan.video_segments.segments.size()+
        (plan.audio_segments?plan.audio_segments->segments.size():0);
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
            if(!sink_.publish(*active_))
                return failed(HlsCacheFailure::UnableToPublish);
            state_=HlsCacheState::Ready;
            active_.reset();
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
        }
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
        if(track_==0 && !active_->audio_segments &&
           active_->selected.direct_media_playlist) {
            if(!active_->source.external_aac_url.empty() && aac_reader_) {
                track_=2; // download, validate and attach standalone AAC
                return state_;
            }
            if(!muxed_audio_verified_) {
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
    if(track_==0 && !is_init && !active_->audio_segments &&
       active_->selected.direct_media_playlist) {
        const auto streams=inspect_mpeg_ts(bytes.bytes);
        if(streams.recognized && streams.h264 && streams.aac)
            muxed_audio_verified_=true;
        // No AAC PMT entry -> do not publish a silent video. The caller
        // must locate a separate reviewed audio rendition first.
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
    return state_;
}

void HlsCacheJob::cancel() noexcept {
    if(state_==HlsCacheState::Fetching) {
        sink_.abort();
        state_=HlsCacheState::Cancelled;
    }
    active_.reset();
}
} // namespace ttnx::playback
