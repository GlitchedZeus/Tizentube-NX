#pragma once

#include "tizentube_nx/playback/media_download.hpp"
#include "tizentube_nx/playback/playback.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ttnx::playback {

enum class ProgressiveStage { Idle, Downloading, Ready, Error, Cancelled };
enum class ProgressiveError {
    None, InvalidId, ResolverUnavailable, IdentityMismatch,
    ShortsForbidden, NoSafeMuxedMp4, CacheUnavailable, DownloadFailed
};

// First end-to-end path to a real normal YouTube MP4, without any ad web player.
// A YouTube Search Video result supplies a normal ID. GuestDirectExtractor
// obtains direct stream URLs, this picks a verified H264/AAC *muxed* MP4,
// MediaDownloadJob fetches bounded ranges into a temporary SD sink, and only
// the complete, verified file can be passed to the offline mpv player.
//
// Network work MUST run on a background worker, never the Borealis UI thread.
// The native media socket adapter remains experimental/opt-in and no existing
// M2 network allowance or hardware-tested NRO is changed by these types.
class ProgressivePrepareJob {
public:
    ProgressivePrepareJob(Extractor& resolver, MediaDownloadJob& downloader)
        : resolver_(resolver), downloader_(downloader) {}
    [[nodiscard]] bool start(std::string_view normal_video_id);
    [[nodiscard]] ProgressiveStage pump();
    void cancel() noexcept;
    [[nodiscard]] ProgressiveStage stage() const noexcept { return stage_; }
    [[nodiscard]] ProgressiveError error() const noexcept { return error_; }
    [[nodiscard]] const std::string& video_id() const noexcept { return video_id_; }
    [[nodiscard]] std::uint64_t duration_ms() const noexcept { return duration_ms_; }
    [[nodiscard]] std::uint64_t received_bytes() const noexcept {
        return downloader_.bytes_received();
    }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        return downloader_.total_bytes();
    }
private:
    Extractor& resolver_;
    MediaDownloadJob& downloader_;
    ProgressiveStage stage_{ProgressiveStage::Idle};
    ProgressiveError error_{ProgressiveError::None};
    std::string video_id_;
    std::uint64_t duration_ms_{0};
};

} // namespace ttnx::playback
