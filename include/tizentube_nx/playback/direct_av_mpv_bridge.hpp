// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tizentube_nx/playback/bounded_direct_track.hpp"
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ttnx::playback {
// Source URLs never enter libmpv: two fixed local stream callback URIs only.
// Keep this bridge and its tracks alive until mpv_terminate_destroy returns.
// Custom stream callbacks perform bounded reviewed 206 GETs (not mpv HTTP).
class DirectAvMpvBridge final {
public:
    static constexpr const char* kScheme="ttnxav";
    static constexpr const char* kVideo="ttnxav://video";
    static constexpr const char* kAudio="ttnxav://audio";
    [[nodiscard]] static std::string edl_uri() {
        // mpv EDL's !new_stream gives the two independently indexed MP4
        // streams a common playback timeline; no physical mux is written.
        const std::string_view video{kVideo},audio{kAudio};
        return "edl://%"+std::to_string(video.size())+"%"+
            std::string(video)+";!new_stream;%"+
            std::to_string(audio.size())+"%"+std::string(audio);
    }
    DirectAvMpvBridge(std::shared_ptr<BoundedDirectTrack> video,
                      std::shared_ptr<BoundedDirectTrack> audio)
        :video_(std::move(video)),audio_(std::move(audio)){}
    DirectAvMpvBridge(const DirectAvMpvBridge&)=delete;
    DirectAvMpvBridge& operator=(const DirectAvMpvBridge&)=delete;
    [[nodiscard]] int register_before_initialize(mpv_handle* mpv)noexcept {
        if(!mpv||!video_||!audio_ || video_->size()<=0 ||
           audio_->size()<=0)return MPV_ERROR_INVALID_PARAMETER;
        return mpv_stream_cb_add_ro(mpv,kScheme,this,&DirectAvMpvBridge::open);
    }
    void cancel()noexcept {
        if(video_)video_->cancel();
        if(audio_)audio_->cancel();
    }
    static int open(void* userdata,char* uri,mpv_stream_cb_info* info) {
        auto* self=static_cast<DirectAvMpvBridge*>(userdata);
        if(!self||!uri||!info)return MPV_ERROR_LOADING_FAILED;
        std::shared_ptr<BoundedDirectTrack> track;
        if(std::strcmp(uri,kVideo)==0)track=self->video_;
        else if(std::strcmp(uri,kAudio)==0)track=self->audio_;
        else return MPV_ERROR_LOADING_FAILED;
        if(!track||track->size()<=0)return MPV_ERROR_LOADING_FAILED;
        auto* cookie=new(std::nothrow) Cookie{std::move(track)};
        if(!cookie)return MPV_ERROR_LOADING_FAILED;
        info->cookie=cookie;
        info->read_fn=&read;
        info->seek_fn=&seek;
        info->size_fn=&size;
        info->cancel_fn=&cancel_cookie;
        info->close_fn=&close_cookie;
        return 0;
    }
private:
    struct Cookie {
        std::shared_ptr<BoundedDirectTrack> track;
        std::atomic<std::uint64_t> pos{0};
        std::atomic<bool> cancelled{false};
    };
    static std::int64_t read(void* ptr,char* dest,std::uint64_t bytes) {
        if(!ptr || !dest || bytes==0)return MPV_ERROR_GENERIC;
        auto* c=static_cast<Cookie*>(ptr);
        if(c->cancelled.load(std::memory_order_acquire))
            return MPV_ERROR_GENERIC;
        auto n=c->track->read_at(c->pos.load(std::memory_order_acquire),
                                dest,std::min<std::uint64_t>(
                                    bytes,BoundedDirectTrack::kMaxRead));
        if(n>0)c->pos.fetch_add(static_cast<std::uint64_t>(n),
                              std::memory_order_acq_rel);
        return n<0?MPV_ERROR_GENERIC:n;
    }
    static std::int64_t seek(void* ptr,std::int64_t absolute) {
        if(!ptr||absolute<0)return MPV_ERROR_UNSUPPORTED;
        auto* c=static_cast<Cookie*>(ptr);
        if(c->cancelled.load(std::memory_order_acquire) ||
           absolute>c->track->size())return MPV_ERROR_UNSUPPORTED;
        c->pos.store(static_cast<std::uint64_t>(absolute),
                     std::memory_order_release);
        return absolute;
    }
    static std::int64_t size(void* ptr) {
        if(!ptr)return MPV_ERROR_UNSUPPORTED;
        auto* c=static_cast<Cookie*>(ptr);
        return c->track->size();
    }
    static void cancel_cookie(void* ptr) {
        if(!ptr)return;
        auto* c=static_cast<Cookie*>(ptr);
        c->cancelled.store(true,std::memory_order_release);
        c->track->cancel();
    }
    static void close_cookie(void* ptr) {
        delete static_cast<Cookie*>(ptr);
    }
    std::shared_ptr<BoundedDirectTrack> video_,audio_;
};
} // namespace ttnx::playback
