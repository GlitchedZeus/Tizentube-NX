#include "tizentube_nx/playback/media_download.hpp"

#include "tizentube_nx/playback/playback.hpp"

#include <algorithm>
#include <utility>

namespace ttnx::playback {

bool MediaDownloadJob::start(std::string signed_url) {
    cancel();
    error_ = MediaDownloadError::None;
    received_ = 0;
    total_ = 0;
    if (!is_reviewable_googlevideo_https_url(signed_url)) {
        error_ = MediaDownloadError::UnsafeUrl;
        state_ = MediaDownloadState::Error;
        return false;
    }
    if (!sink_.begin()) {
        sink_.abort();
        error_ = MediaDownloadError::SinkFailure;
        state_ = MediaDownloadState::Error;
        return false;
    }
    sink_open_ = true;
    url_ = std::move(signed_url);
    state_ = MediaDownloadState::Fetching;
    return true;
}

MediaDownloadState MediaDownloadJob::pump() {
    if (state_ != MediaDownloadState::Fetching) return state_;
    const auto range = next_media_range(received_, total_);
    if (!range) {
        fail(MediaDownloadError::RangeFailure);
        return state_;
    }
    const auto bytes = reader_.read(url_, *range);
    if (!bytes.ok()) {
        fail(MediaDownloadError::RangeFailure);
        return state_;
    }
    if (bytes.resource_size == 0 || bytes.resource_size > kMaxFileBytes) {
        fail(MediaDownloadError::ResourceTooLarge);
        return state_;
    }
    if (total_ != 0 && total_ != bytes.resource_size) {
        fail(MediaDownloadError::ResourceChanged);
        return state_;
    }
    if (total_ == 0) total_ = bytes.resource_size;

    // When the first response arrives, the total size is discovered.
    // A small resource can yield 206 with fewer bytes than the initial
    // requested chunk; accept only if the returned range and size are
    // self-consistent and within the selected request envelope.
    if (bytes.bytes.empty() ||
        bytes.bytes.size() > kMaxFileBytes - received_) {
        fail(MediaDownloadError::ResourceTooLarge);
        return state_;
    }
    if (!sink_.append(bytes.bytes)) {
        fail(MediaDownloadError::SinkFailure);
        return state_;
    }
    received_ += static_cast<std::uint64_t>(bytes.bytes.size());
    if (received_ > total_) {
        fail(MediaDownloadError::ResourceChanged);
        return state_;
    }
    if (received_ == total_) {
        if (!sink_.finish()) {
            fail(MediaDownloadError::SinkFailure);
            return state_;
        }
        sink_open_ = false;
        url_.clear();
        state_ = MediaDownloadState::Complete;
    }
    return state_;
}

void MediaDownloadJob::fail(MediaDownloadError code) noexcept {
    if (sink_open_) {
        sink_.abort();
        sink_open_ = false;
    }
    url_.clear();
    error_ = code;
    state_ = MediaDownloadState::Error;
}

void MediaDownloadJob::cancel() noexcept {
    if (sink_open_) {
        sink_.abort();
        sink_open_ = false;
    }
    url_.clear();
    if (state_ == MediaDownloadState::Fetching)
        state_ = MediaDownloadState::Cancelled;
}

} // namespace ttnx::playback
