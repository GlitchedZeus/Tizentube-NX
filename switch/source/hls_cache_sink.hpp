#pragma once
#ifdef TTNX_M3_MEDIA_PLANE
#include "tizentube_nx/playback/hls_cache.hpp"

#include <string>
#include <string_view>

namespace ttnx::switch_app {

// Disposable app-owned HLS directory, never a source save or emulator path.
// The accepted M2 NRO does not compile this feature. Atomic-ish publishing
// retains the old ready directory until the entire new clip is flushed.
// No media-signed URL appears in any local playlist.
class SwitchHlsCacheSink final: public playback::HlsCacheSink {
public:
    ~SwitchHlsCacheSink() override { abort(); }
    SwitchHlsCacheSink()=default;
    SwitchHlsCacheSink(const SwitchHlsCacheSink&)=delete;
    SwitchHlsCacheSink& operator=(const SwitchHlsCacheSink&)=delete;
    bool begin(const playback::HlsProbePlan& plan) override;
    bool write_segment(bool audio,bool init,
                       std::size_t index,std::string_view bytes) override;
    bool publish(const playback::HlsProbePlan& plan) override;
    void abort() noexcept override;

#ifdef TTNX_M3_CACHE_HOST_TEST
    static constexpr const char* kReadyMaster=
        "/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/ready/master.m3u8";
#else
    static constexpr const char* kReadyMaster=
        "sdmc:/switch/TizenTube-NX/media-cache/hls/ready/master.m3u8";
#endif
private:
    bool staged_{false};
    bool wrote_video_{false};
    std::string video_ext_{"m4s"},audio_ext_{"m4s"};
};
} // namespace ttnx::switch_app
#endif // TTNX_M3_MEDIA_PLANE
