// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(TTNX_M4_DIRECT_AV_PROBE) && defined(TTNX_M3_INTEGRATED)
#include "tizentube_nx/playback/direct_av_plan.hpp"
#include "tizentube_nx/playback/direct_av_mpv_bridge.hpp"
#include "libnx_http_client.hpp"
#include <memory>
#include <utility>

namespace ttnx::switch_app {
// Zero SD media storage. Two independent libnx SSL transport owners prevent
// mpv's concurrently demuxed video/audio from sharing an unsafe socket.
// Creation + HTTP prime must run on the media worker, never the UI thread.
class NativeDirectAvSession final {
public:
    static std::shared_ptr<NativeDirectAvSession> create(
        const playback::DirectAvPlan& approved) {
        auto owner=std::shared_ptr<NativeDirectAvSession>(
            new NativeDirectAvSession);
        if(!owner->video_http_.ready()||!owner->audio_http_.ready())return {};
        owner->video_=std::make_shared<playback::BoundedDirectTrack>(
            owner->video_reader_,approved.video_url);
        owner->audio_=std::make_shared<playback::BoundedDirectTrack>(
            owner->audio_reader_,approved.audio_url);
        if(!owner->video_->prime() || !owner->audio_->prime())return {};
        owner->bridge_=std::make_unique<playback::DirectAvMpvBridge>(
            owner->video_,owner->audio_);
        owner->duration_ms_=approved.duration_ms;
        return owner;
    }
    NativeDirectAvSession(const NativeDirectAvSession&)=delete;
    NativeDirectAvSession& operator=(const NativeDirectAvSession&)=delete;
    [[nodiscard]] int register_decoder(mpv_handle* mpv) {
        return bridge_?bridge_->register_before_initialize(mpv):
            MPV_ERROR_INVALID_PARAMETER;
    }
    [[nodiscard]] static std::string edl_uri() {
        return playback::DirectAvMpvBridge::edl_uri();
    }
    [[nodiscard]] std::uint64_t duration_ms()const noexcept {
        return duration_ms_;
    }
    [[nodiscard]] std::uint64_t range_requests()const noexcept {
        return (video_?video_->fetched_ranges():0)+
               (audio_?audio_->fetched_ranges():0);
    }
    [[nodiscard]] std::uint64_t retained_bytes()const noexcept {
        return (video_?video_->retained_bytes():0)+
               (audio_?audio_->retained_bytes():0);
    }
    void cancel()noexcept {if(bridge_)bridge_->cancel();}
private:
    NativeDirectAvSession()
        :video_reader_(video_http_),audio_reader_(audio_http_){}
    // Keep network transports alive LONGER than readers/tracks/decoder.
    LibnxMediaChunkTransport video_http_,audio_http_;
    playback::MediaChunkReader video_reader_,audio_reader_;
    std::shared_ptr<playback::BoundedDirectTrack> video_,audio_;
    std::unique_ptr<playback::DirectAvMpvBridge> bridge_{};
    std::uint64_t duration_ms_{0};
};
} // namespace ttnx::switch_app
#endif
