#pragma once

#include "tizentube_nx/playback/media_chunks.hpp"

#include <cstdint>
#include <string>

namespace ttnx::playback {

// Owner-controlled *temporary* cache contract. The implementation must stage
// writes in its own cache path, atomically publish only on finish(), and
// discard incomplete data on abort(). It must never write user/source saves.
class MediaChunkSink {
public:
    virtual ~MediaChunkSink() = default;
    virtual bool begin() = 0;
    virtual bool append(std::string_view bytes) = 0;
    virtual bool finish() = 0;
    virtual void abort() noexcept = 0;
};

enum class MediaDownloadState { Idle, Fetching, Complete, Error, Cancelled };
enum class MediaDownloadError {
    None, UnsafeUrl, SinkFailure, RangeFailure,
    ResourceChanged, ResourceTooLarge
};

// Bounded, resumable-by-pump transfer coordinator for progressive MP4 caching.
// One bounded HTTP read per pump(); no network calls occur in start/cancel.
// It does NOT automatically authorize Googlevideo or set up network sockets.
// Before actual Switch use, the socket backend must enforce media policy
// *before DNS* and this worker must run away from Borealis UI threads.
class MediaDownloadJob {
public:
    static constexpr std::uint64_t kMaxFileBytes = 512ULL * 1024 * 1024;
    MediaDownloadJob(MediaChunkReader& reader, MediaChunkSink& sink)
        : reader_(reader), sink_(sink) {}
    ~MediaDownloadJob() { cancel(); }
    MediaDownloadJob(const MediaDownloadJob&) = delete;
    MediaDownloadJob& operator=(const MediaDownloadJob&) = delete;

    [[nodiscard]] bool start(std::string signed_url);
    [[nodiscard]] MediaDownloadState pump();
    void cancel() noexcept;
    [[nodiscard]] MediaDownloadState state() const noexcept { return state_; }
    [[nodiscard]] MediaDownloadError error() const noexcept { return error_; }
    [[nodiscard]] std::uint64_t bytes_received() const noexcept { return received_; }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_; }
private:
    void fail(MediaDownloadError error) noexcept;
    MediaChunkReader& reader_;
    MediaChunkSink& sink_;
    std::string url_; // ephemeral bearer-like URL, never serialized
    MediaDownloadState state_{MediaDownloadState::Idle};
    MediaDownloadError error_{MediaDownloadError::None};
    std::uint64_t received_{0};
    std::uint64_t total_{0};
    bool sink_open_{false};
};

} // namespace ttnx::playback
