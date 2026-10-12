#ifdef TTNX_M3_MEDIA_PLANE

#include "media_file_sink.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace ttnx::switch_app {
namespace {
bool ensure_folder(const char* path) {
    if (mkdir(path, 0700) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat st{};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

bool SwitchMediaCacheSink::begin() {
    // The native player also acquires this lease for completed MP4.
    // Never overwrite a file while libmpv could have its descriptors open.
    if(playback::HlsPlaybackLease::is_active())return false;
    abort();
    // No user-input path manipulation. The only SD writes stay under this
    // app-owned disposable cache folder, never a save/emulator directory.
    if (!ensure_folder("sdmc:/switch/TizenTube-NX") ||
        !ensure_folder("sdmc:/switch/TizenTube-NX/media-cache")) return false;
    (void)std::remove(kPartial); // stale/incomplete previous download only
    file_ = std::fopen(kPartial, "wb");
    size_ = 0;
    return file_ != nullptr;
}

bool SwitchMediaCacheSink::append(std::string_view bytes) {
    if (!file_ || bytes.empty() ||
        bytes.size() > playback::MediaChunkReader::kMaxChunkBytes ||
        size_ > playback::MediaDownloadJob::kMaxFileBytes ||
        bytes.size() > playback::MediaDownloadJob::kMaxFileBytes - size_)
        return false;
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), file_);
    if (written != bytes.size()) return false;
    size_ += written;
    return true;
}

bool SwitchMediaCacheSink::finish() {
    if (!file_ || size_ == 0 ||
        playback::HlsPlaybackLease::is_active())return false;
    // Commit only after all media bytes have been received and durably flushed.
    if (std::fflush(file_) != 0) return false;
    if (fsync(fileno(file_)) != 0) return false;
    if (std::fclose(file_) != 0) {
        file_ = nullptr;
        return false;
    }
    file_ = nullptr;
    if (std::rename(kPartial, kComplete) != 0) return false;
    size_ = 0;
    return true;
}

void SwitchMediaCacheSink::abort() noexcept {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    (void)std::remove(kPartial);
    size_ = 0;
}

} // namespace ttnx::switch_app
#endif // TTNX_M3_MEDIA_PLANE
