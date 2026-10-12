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
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
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
void SwitchMediaCacheSink::invalidate_adaptive_pair() noexcept {
    // Invalidate the two-file transaction before either side is replaced.
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
    if(slot_!=Slot::MuxedVideo)invalidate_adaptive_pair();
    (void)std::remove(partial());
    file_=std::fopen(partial(),"wb");
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
    if (std::rename(partial(),complete()) != 0) return false;
    size_ = 0;
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
