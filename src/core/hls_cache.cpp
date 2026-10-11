#include "tizentube_nx/playback/hls_cache.hpp"

#include <utility>

namespace ttnx::playback {
bool HlsCacheJob::start(const HlsProbePlan& plan) {
    cancel();
    active_.reset();
    bytes_=0;
    completed_=0;
    next_segment_=0;
    track_=0;
    error_=HlsCacheFailure::InvalidPlaylist;
    state_=HlsCacheState::Failed;
    const auto count=plan.video_segments.segments.size()+
        (plan.audio_segments?plan.audio_segments->segments.size():0);
    if(count==0 || count>4096 ||
       !plan.selected.height || !plan.selected.fps ||
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

    const auto& playlist=track_==0
        ?active_->video_segments:*active_->audio_segments;
    if(next_segment_>=playlist.segments.size() && !init_pending_) {
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
