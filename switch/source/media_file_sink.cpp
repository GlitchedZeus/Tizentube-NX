#ifdef TTNX_M3_MEDIA_PLANE

#include "media_file_sink.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include "tizentube_nx/playback/aac_mp4_structure.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

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
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

namespace {
bool inspect_file(const char* path,std::uint64_t bound,
                  std::uint64_t& length) noexcept {
    std::FILE* f=std::fopen(path,"rb");
    if(!f)return false;
    if(std::fseek(f,0,SEEK_END)!=0) {std::fclose(f);return false;}
    const long size=std::ftell(f);
    if(size<1024 || static_cast<std::uint64_t>(size)>bound ||
       std::fseek(f,0,SEEK_SET)!=0) {std::fclose(f);return false;}
    playback::AacMp4Structure structure;
    if(!structure.begin(static_cast<std::uint64_t>(size),bound)) {
        std::fclose(f);return false;
    }
    char bytes[65536];
    bool ok=true;
    while(true) {
        const auto got=std::fread(bytes,1,sizeof(bytes),f);
        if(got && !structure.append(std::string_view(bytes,got))){ok=false;break;}
        if(got<sizeof(bytes)){
            if(std::ferror(f))ok=false;
            break;
        }
    }
    std::fclose(f);
    if(!ok || !structure.complete())return false;
    length=static_cast<std::uint64_t>(size);
    return true;
}
}
const char* SwitchMediaCacheSink::partial() const noexcept {
    switch(slot_) {
      case Slot::MuxedVideo:return kPartial;
      case Slot::AdaptiveVideo:
#ifdef TTNX_M3_CACHE_HOST_TEST
        return "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_video.mp4.part";
#else
        return "sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4.part";
#endif
      case Slot::AdaptiveAudio:
#ifdef TTNX_M3_CACHE_HOST_TEST
        return "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.part";
#else
        return "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.part";
#endif
    }
    return kPartial;
}
const char* SwitchMediaCacheSink::complete() const noexcept {
    if(slot_==Slot::AdaptiveVideo)return kAdaptiveVideo;
    if(slot_==Slot::AdaptiveAudio)return kAdaptiveAudio;
    return kComplete;
}
const char* SwitchMediaCacheSink::backup() const noexcept {
#ifdef TTNX_M3_CACHE_HOST_TEST
    switch(slot_) {
      case Slot::MuxedVideo:
        return "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/active.mp4.previous";
      case Slot::AdaptiveVideo:
        return "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_video.mp4.previous";
      case Slot::AdaptiveAudio:
        return "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.previous";
    }
#else
    switch(slot_) {
      case Slot::MuxedVideo:
        return "sdmc:/switch/TizenTube-NX/media-cache/active.mp4.previous";
      case Slot::AdaptiveVideo:
        return "sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4.previous";
      case Slot::AdaptiveAudio:
        return "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.previous";
    }
#endif
    return kComplete;
}
bool SwitchMediaCacheSink::discard_session_media() noexcept {
    if(playback::HlsPlaybackLease::is_active())return false;
    // A symlinked cache folder must never redirect our known-name unlinks
    // into unrelated SD locations. lstat every traversed app-owned root.
    struct stat parent{},root{};
#ifdef TTNX_M3_CACHE_HOST_TEST
    constexpr const char* app="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX";
#else
    constexpr const char* app="sdmc:/switch/TizenTube-NX";
#endif
    if(lstat(app,&parent)!=0)return errno==ENOENT;
    if(!S_ISDIR(parent.st_mode))return false;
    if(lstat(kFolder,&root)!=0)return errno==ENOENT;
    if(!S_ISDIR(root.st_mode))return false;
    // Never enumerate arbitrary SD contents, remove unknown filenames,
    // follow symlinks, or touch configuration, logs or emulators.
    static constexpr const char* names[]={
        kComplete,kPartial,
        kAdaptiveVideo,kAdaptiveAudio,kAdaptiveMarker,
#ifdef TTNX_M3_CACHE_HOST_TEST
        "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/active.mp4.previous",
        "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_video.mp4.part",
        "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_video.mp4.previous",
        "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.part",
        "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.previous",
        "/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_verified.txt.part"
#else
        "sdmc:/switch/TizenTube-NX/media-cache/active.mp4.previous",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4.part",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4.previous",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.part",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a.previous",
        "sdmc:/switch/TizenTube-NX/media-cache/adaptive_verified.txt.part"
#endif
    };
    bool okay=true;
    for(const char* path:names) {
        struct stat entry{};
        if(lstat(path,&entry)!=0) {
            if(errno!=ENOENT)okay=false;
            continue;
        }
        if(!S_ISREG(entry.st_mode) || unlink(path)!=0)okay=false;
    }
    return okay;
}
void SwitchMediaCacheSink::invalidate_adaptive_pair() noexcept {
    // Refuse to invalidate a published marker while libmpv might still
    // use either file. This also protects Replay from a second Search.
    if(playback::HlsPlaybackLease::is_active())return;
    (void)std::remove(kAdaptiveMarker);
}
bool SwitchMediaCacheSink::adaptive_pair_valid() noexcept {
    std::uint64_t v=0,a=0;
    if(!inspect_file(kAdaptiveVideo,512ULL*1024*1024,v) ||
       !inspect_file(kAdaptiveAudio,128ULL*1024*1024,a))return false;
    std::FILE* marker=std::fopen(kAdaptiveMarker,"rb");
    if(!marker)return false;
    unsigned long long marked_v=0,marked_a=0;
    char trailing=0;
    const bool exact=std::fscanf(marker,"%llu %llu%c",
                         &marked_v,&marked_a,&trailing)==3 &&
                     trailing=='\n' && std::fgetc(marker)==EOF;
    std::fclose(marker);
    return exact && marked_v==v && marked_a==a;
}
bool SwitchMediaCacheSink::publish_adaptive_pair() noexcept {
    if(playback::HlsPlaybackLease::is_active())return false;
    // Preserve fail-closed partial state if any file was replaced/torn.
    invalidate_adaptive_pair();
    std::uint64_t v=0,a=0;
    if(!inspect_file(kAdaptiveVideo,512ULL*1024*1024,v) ||
       !inspect_file(kAdaptiveAudio,128ULL*1024*1024,a))return false;
    const std::string tmp=std::string(kAdaptiveMarker)+".part";
    std::FILE* out=std::fopen(tmp.c_str(),"wb");
    if(!out)return false;
    const std::string content=std::to_string(v)+" "+std::to_string(a)+"\n";
    const bool success=std::fwrite(content.data(),1,content.size(),out)==content.size() &&
                       std::fflush(out)==0 && fsync(fileno(out))==0;
    const bool close_ok=std::fclose(out)==0;
    if(!success || !close_ok ||
       std::rename(tmp.c_str(),kAdaptiveMarker)!=0) {
        (void)std::remove(tmp.c_str());return false;
    }
    return true;
}

bool SwitchMediaCacheSink::begin() {
    last_error_step_=StorageStep::None;
    last_error_errno_=0;
    // The native player also acquires this lease for completed MP4.
    // Never overwrite a file while libmpv could have its descriptors open.
    if(playback::HlsPlaybackLease::is_active())return false;
    abort();
    // No user-input path manipulation. The only SD writes stay under this
    // app-owned disposable cache folder, never a save/emulator directory.
#ifdef TTNX_M3_CACHE_HOST_TEST
    if(!ensure_folder("/tmp/ttnx-adaptive-cache-test") ||
       !ensure_folder("/tmp/ttnx-adaptive-cache-test/switch") ||
       !ensure_folder("/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX") ||
       !ensure_folder(kFolder))return false;
#else
    if(!ensure_folder("sdmc:/switch/TizenTube-NX") ||
       !ensure_folder(kFolder))return false;
#endif
    // Recover a previous complete cache file after power loss between
    // backup and promotion. Only these fixed app-owned paths are touched.
    struct stat current{},previous{};
    const bool exists=lstat(complete(),&current)==0;
    const bool saved=lstat(backup(),&previous)==0;
    if((exists && !S_ISREG(current.st_mode)) ||
       (saved && !S_ISREG(previous.st_mode))) {
        record_failure(StorageStep::Open,EINVAL);
        return false;
    }
    if(!exists && saved && std::rename(backup(),complete())!=0) {
        record_failure(StorageStep::Restore,errno);
        return false;
    }
    if(slot_!=Slot::MuxedVideo)invalidate_adaptive_pair();
    (void)std::remove(partial());
    file_=std::fopen(partial(),"wb");
    size_ = 0;
    if(!file_)record_failure(StorageStep::Open,errno);
    return file_ != nullptr;
}

bool SwitchMediaCacheSink::append(std::string_view bytes) {
    if (!file_ || bytes.empty() ||
        bytes.size() > playback::MediaChunkReader::kMaxChunkBytes ||
        size_ > playback::MediaDownloadJob::kMaxFileBytes ||
        bytes.size() > playback::MediaDownloadJob::kMaxFileBytes - size_)
        return false;
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), file_);
    if (written != bytes.size()) {
        record_failure(StorageStep::Write,errno ? errno : EIO);
        return false;
    }
    size_ += written;
    return true;
}

bool SwitchMediaCacheSink::finish() {
    if (!file_ || size_ == 0 || playback::HlsPlaybackLease::is_active()) {
        record_failure(StorageStep::Flush,EBUSY);
        return false;
    }
    // Flush and sync before closing; never weaken SD integrity requirements.
#ifdef TTNX_M3_CACHE_HOST_TEST
    if(test_failure_==StorageStep::Flush) {
        record_failure(StorageStep::Flush,EIO);return false;
    }
#endif
    if(std::fflush(file_)!=0) {
        record_failure(StorageStep::Flush,errno);return false;
    }
#ifdef TTNX_M3_CACHE_HOST_TEST
    if(test_failure_==StorageStep::Sync) {
        record_failure(StorageStep::Sync,EIO);return false;
    }
#endif
    if(fsync(fileno(file_))!=0) {
        record_failure(StorageStep::Sync,errno);return false;
    }
#ifdef TTNX_M3_CACHE_HOST_TEST
    if(test_failure_==StorageStep::Close) {
        record_failure(StorageStep::Close,EIO);return false;
    }
#endif
    const int close_rc=std::fclose(file_);
    file_=nullptr;
    if(close_rc!=0) {
        record_failure(StorageStep::Close,errno);return false;
    }
    // The download may be complete in RAM while the underlying SD file
    // is shorter. Reject it before replacing an existing playable file.
    struct stat pending{},old{},saved{};
    if(lstat(partial(),&pending)!=0 ||
       !S_ISREG(pending.st_mode) ||
       pending.st_size<0 ||
       static_cast<std::uint64_t>(pending.st_size)!=size_) {
        record_failure(StorageStep::Size,errno ? errno : EIO);return false;
    }
    // Horizon's fsFsRenameFile does not provide the POSIX guarantee
    // that rename(src, existing_dst) atomically replaces dst. Preserve
    // an older completed cache, free the destination, then promote;
    // rollback on failure. This is only for fixed disposable media paths.
    const bool have_old=lstat(complete(),&old)==0;
    if(have_old && !S_ISREG(old.st_mode)) {
        record_failure(StorageStep::Backup,EINVAL);return false;
    }
    if(lstat(backup(),&saved)==0) {
        if(!S_ISREG(saved.st_mode) || std::remove(backup())!=0) {
            record_failure(StorageStep::Backup,errno ? errno : EINVAL);return false;
        }
    }
    if(have_old) {
        if(std::rename(complete(),backup())!=0) {
            record_failure(StorageStep::Backup,errno);return false;
        }
    }
#ifdef TTNX_M3_CACHE_HOST_TEST
    const bool simulated=test_failure_==StorageStep::Promote;
    if(simulated)errno=EIO;
#else
    const bool simulated=false;
#endif
    if(simulated || std::rename(partial(),complete())!=0) {
        const int promotion_errno=errno;
        if(have_old && std::rename(backup(),complete())!=0) {
            record_failure(StorageStep::Restore,errno);return false;
        }
        record_failure(StorageStep::Promote,promotion_errno);
        return false;
    }
    if(have_old)(void)std::remove(backup());
    last_error_step_=StorageStep::None;
    last_error_errno_=0;
    size_=0;
    return true;
}

void SwitchMediaCacheSink::abort() noexcept {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    (void)std::remove(partial());
    size_ = 0;
}

} // namespace ttnx::switch_app
#endif // TTNX_M3_MEDIA_PLANE
