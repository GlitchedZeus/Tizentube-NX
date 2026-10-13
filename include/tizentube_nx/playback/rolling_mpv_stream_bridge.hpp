// SPDX-License-Identifier: GPL-3.0-or-later
// mpv custom-stream model inspired by mirusu400/switch-newpipe's
// switchcache:// stream callbacks (GPL-3.0), upstream SHA 1cd590e21280...
// Original bounded-rendezvous and owned-reader integration for TizenTube NX.
#pragma once
#include "tizentube_nx/playback/rolling_file_stream.hpp"
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#include <cstring>
#include <memory>

namespace ttnx::playback {
class RollingMpvStreamBridge final {
public:
    static constexpr const char* kProtocol="ttnxring";
    static constexpr const char* kURI="ttnxring://media";
    explicit RollingMpvStreamBridge(std::shared_ptr<RollingFileStream> file)
        :file_(std::move(file)){}
    RollingMpvStreamBridge(const RollingMpvStreamBridge&)=delete;
    RollingMpvStreamBridge& operator=(const RollingMpvStreamBridge&)=delete;
    // Register BEFORE mpv_initialize. Keep this object alive until mpv dies.
    [[nodiscard]] int register_before_initialize(mpv_handle* mpv) noexcept {
        if(!file_ || !mpv)return MPV_ERROR_INVALID_PARAMETER;
        return mpv_stream_cb_add_ro(mpv,kProtocol,this,&RollingMpvStreamBridge::open);
    }
    static int open(void* userdata,char* uri,mpv_stream_cb_info* info) {
        auto* self=static_cast<RollingMpvStreamBridge*>(userdata);
        if(!self || !self->file_ || !uri || !info ||
           std::strcmp(uri,kURI)!=0)return MPV_ERROR_LOADING_FAILED;
        auto reader=self->file_->open_reader();
        if(!reader)return MPV_ERROR_LOADING_FAILED;
        info->cookie=reader.release();
        info->read_fn=&read;
        info->seek_fn=&seek;
        // Deliberately null. Upstream discovered FFmpeg mov_read_mfra()
        // seeks to unavailable end indexes when told a growing cache size.
        info->size_fn=nullptr;
        info->close_fn=&close;
        info->cancel_fn=&cancel;
        return 0;
    }
    static std::int64_t read(void* cookie,char* output,std::uint64_t bytes) {
        if(!cookie)return MPV_ERROR_GENERIC;
        const auto result=static_cast<RollingFileStream::Reader*>(cookie)->read(output,bytes);
        return result<0?MPV_ERROR_GENERIC:result;
    }
    static std::int64_t seek(void* cookie,std::int64_t absolute) {
        if(!cookie)return MPV_ERROR_UNSUPPORTED;
        const auto result=static_cast<RollingFileStream::Reader*>(cookie)->seek(absolute);
        return result<0?MPV_ERROR_UNSUPPORTED:result;
    }
    static void cancel(void* cookie) {
        if(!cookie)return;
        // Upstream libmpv invokes cancel on a separate thread to break an
        // in-progress read. This cancels the producer/consumer epoch too.
        static_cast<RollingFileStream::Reader*>(cookie)->cancel();
    }
    static void close(void* cookie) {
        delete static_cast<RollingFileStream::Reader*>(cookie);
    }
private:
    std::shared_ptr<RollingFileStream> file_;
};
} // namespace ttnx::playback
