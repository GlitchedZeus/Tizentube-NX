#pragma once
#ifdef TTNX_M3_MEDIA_PLANE
#include "tizentube_nx/playback/hls_cache.hpp"

#include <string>
#include <string_view>
#include <vector>

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
    bool append_external_aac(std::string_view bytes) override;
    bool stage_startup_event(const playback::HlsProbePlan& plan,
                             std::size_t prefix_count) override;
    bool publish(const playback::HlsProbePlan& plan) override;
    void abort() noexcept override;
    // Delete only our own fixed HLS ready/pending/previous directories,
    // and only after all libmpv descriptors are closed.
    [[nodiscard]] static bool discard_session_media() noexcept;

#ifdef TTNX_M3_CACHE_HOST_TEST
    static constexpr const char* kReadyAudioMode=
        "/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/ready/audio_mode.txt";
    static constexpr const char* kReadyExternalAacBytes=
        "/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/ready/external_audio.bytes";
    static constexpr const char* kReadyMaster=
        "/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/ready/master.m3u8";
    static constexpr const char* kReadyExternalAac=
        "/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/ready/external_audio.m4a";
#else
    static constexpr const char* kReadyAudioMode=
        "sdmc:/switch/TizenTube-NX/media-cache/hls/ready/audio_mode.txt";
    static constexpr const char* kReadyExternalAacBytes=
        "sdmc:/switch/TizenTube-NX/media-cache/hls/ready/external_audio.bytes";
    static constexpr const char* kReadyMaster=
        "sdmc:/switch/TizenTube-NX/media-cache/hls/ready/master.m3u8";
    static constexpr const char* kReadyExternalAac=
        "sdmc:/switch/TizenTube-NX/media-cache/hls/ready/external_audio.m4a";
#endif
private:
    bool staged_{false};
    bool wrote_video_{false};
    std::uint64_t external_audio_bytes_{0};
    std::string video_ext_{"m4s"},audio_ext_{"m4s"};
    std::vector<bool> video_written_,audio_written_;
    bool video_init_needed_{false},audio_init_needed_{false};
    bool video_init_written_{false},audio_init_written_{false};
};
} // namespace ttnx::switch_app
#endif // TTNX_M3_MEDIA_PLANE
