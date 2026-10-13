#if defined(TTNX_M4_ROLLING_STREAM) || defined(TTNX_M4_ROLLING_HOST_TEST)
#include "rolling_hls_feed.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
#include "tizentube_nx/playback/ts_streams.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace ttnx::switch_app {
namespace {
bool muxed_ts(const std::string& media) {
    // Each segment must independently contain PAT/PMT declaring H.264 + AAC.
    // Sync evidence alone or HTTP Content-Type is not codec verification.
    if(media.size()<376 || media.size()%188!=0)return false;
    for(std::size_t i=0;i<media.size();i+=188)
        if(static_cast<unsigned char>(media[i])!=0x47)return false;
    const auto info=playback::inspect_mpeg_ts(media);
    return info.recognized && info.h264 && info.aac;
}
}

bool RollingMuxedHlsSession::begin(const playback::HlsProbePlan& plan) {
    if(state_!=State::Idle && state_!=State::Cancelled)return false;
    // No guessed codec/resolution for this initial strictly muxed path.
    // All segments must already have passed the source playlist parser.
    if(!core::is_valid_video_id(plan.source.video_id) ||
       plan.selected.height==0 || plan.selected.height>720 ||
       plan.selected.fps==0 || plan.selected.fps>60 ||
       plan.audio_segments ||
       !plan.selected.audio_playlist_url.empty() ||
       !plan.source.external_aac_url.empty() ||
       plan.video_segments.init_segment_url ||
       plan.video_segments.segments.empty() ||
       plan.video_segments.segments.size()>RollingHlsSink::kMaxSegments ||
       plan.video_segments.total_duration_ms==0 ||
       plan.source.duration_ms==0) {
        error_=Error::InvalidPlan;return false;
    }
    std::uint64_t sum=0,maximum=0;
    std::vector<std::uint64_t> ends;
    ends.reserve(plan.video_segments.segments.size());
    for(const auto& seg:plan.video_segments.segments) {
        if(!seg.duration_ms || seg.duration_ms>60000 ||
           !playback::is_reviewable_hls_child_url(seg.url) ||
           sum>std::numeric_limits<std::uint64_t>::max()-seg.duration_ms) {
            error_=Error::InvalidPlan;return false;
        }
        sum+=seg.duration_ms;
        maximum=std::max(maximum,seg.duration_ms);
        ends.push_back(sum);
    }
    const auto close_enough=[](std::uint64_t a,std::uint64_t b) {
        return a>b ? a-b<=3000 : b-a<=3000;
    };
    if(!close_enough(sum,plan.video_segments.total_duration_ms) ||
       !close_enough(sum,plan.source.duration_ms) ||
       !coordinator_.begin(0,plan.selected.height,plan.selected.fps)) {
        error_=Error::InvalidPlan;return false;
    }
    if(!sink_.begin(maximum)) {
        // Coordinator was initialized, but no media ticket exists. Reset it
        // so an interrupted/failed begin cannot poison a future retry.
        (void)coordinator_.cancel(true);
        error_=Error::UnableToStage;return false;
    }
    segments_=plan.video_segments.segments;
    ends_ms_=std::move(ends);
    next_=first_retained_=0;
    error_=Error::None;
    state_=State::Fetching;
    return true;
}

RollingMuxedHlsSession::State RollingMuxedHlsSession::pump() {
    if(state_!=State::Fetching && state_!=State::PrefixReady &&
       state_!=State::Waiting)return state_;
    if(next_==segments_.size()) {
        if(!sink_.finish())return failure(Error::Manifest);
        state_=State::Complete;
        return state_;
    }
    const auto start=next_==0?0:ends_ms_[next_-1];
    const auto end=ends_ms_[next_];
    using Admission=playback::RollingStreamCoordinator::Admission;
    // Conservatively reserve the full permitted HTTP response. The sink's
    // separate 128-KiB manifest allocation is outside the media-byte limit;
    // never fetch a response that would exceed its physical SD quota.
    if(sink_.bytes()>RollingHlsSink::kMaxMediaBytes-
                        playback::RollingStreamCoordinator::kMaxSegmentBytes) {
        state_=State::Waiting;
        return state_;
    }
    const auto decision=coordinator_.reserve(
        playback::RollingMediaBudget::Track::Muxed,
        static_cast<std::uint32_t>(next_),start,end,
        playback::RollingStreamCoordinator::kMaxSegmentBytes);
    if(decision==Admission::Backpressure ||
       (decision==Admission::Invalid && coordinator_.state()!=
                           playback::RollingStreamCoordinator::State::Failed)) {
        // Future playback window or a full 24 MiB quota: never request the
        // next CDN segment until libmpv confirms old files are disposable.
        state_=State::Waiting;
        return state_;
    }
    if(decision!=Admission::Accepted)return failure(Error::Accounting);
    const auto ticket=*coordinator_.pending_ticket();
    // The native adapter now emits approved HTTPS body chunks directly into
    // a private SD .part; the parser checks Content-Length/MIME before any
    // media can reach the sink. Host fake clients use the legacy adapter.
    std::size_t received=0;
    bool network_ok=false;
    const bool stored=sink_.append_streamed(
        next_,segments_[next_].duration_ms,
        [&](const RollingHlsSink::ChunkEmitter& emit) {
            const auto incoming=reader_.stream(segments_[next_].url,emit);
            network_ok=incoming.ok;
            received=incoming.bytes;
            return incoming.ok;
        });
    if(!stored) {
        coordinator_.cancel_pending();
        return failure(network_ok ? Error::MediaWrite : Error::SegmentHttp);
    }
    // Both PAT/PMT and all 188-byte sync boundaries were checked above.
    // The actual Switch decoder remains responsible for confirming
    // picture/audio before this experimental route is user-visible.
    coordinator_.mark_codecs_verified(true,true);
    if(!coordinator_.committed(ticket,received,true,true))
        return failure(Error::Accounting);
    ++next_;
    state_=coordinator_.startup_ready()?State::PrefixReady:State::Fetching;
    return state_;
}

bool RollingMuxedHlsSession::retire_before(
    std::size_t first_retained_index,std::uint64_t playhead_ms,
    bool decoder_released) {
    if(!decoder_released || (state_!=State::Fetching &&
       state_!=State::Waiting && state_!=State::PrefixReady &&
       state_!=State::Complete) ||
       first_retained_index<=first_retained_ ||
       first_retained_index>=next_ ||
       !coordinator_.advance_playhead(playhead_ms))return false;
    for(auto i=first_retained_;i<first_retained_index;++i) {
        if(ends_ms_[i]>playhead_ms ||
           playhead_ms-ends_ms_[i]<
                 playback::RollingMediaBudget::kEvictionGuardMs)
            return false;
    }
    // The playlist must move first. Then the sink unlinks old files.
    if(!sink_.retire_before(first_retained_index))
        return failure(Error::MediaWrite),false;
    for(auto i=first_retained_;i<first_retained_index;++i) {
        if(!coordinator_.acknowledge_unlinked(
                playback::RollingMediaBudget::Track::Muxed,
                static_cast<std::uint32_t>(i)))
            return failure(Error::Accounting),false;
    }
    first_retained_=first_retained_index;
    if(state_==State::Waiting)state_=State::Fetching;
    return true;
}

bool RollingMuxedHlsSession::close_after_player(bool decoder_closed) noexcept {
    if(!decoder_closed)return false;
    if(!sink_.close_session())return false;
    coordinator_.cancel_pending();
    if(!coordinator_.cancel(true))return false;
    segments_.clear();
    ends_ms_.clear();
    next_=first_retained_=0;
    state_=State::Cancelled;
    return true;
}
} // namespace ttnx::switch_app
#endif
