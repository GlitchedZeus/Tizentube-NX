#pragma once

#ifdef TTNX_M3_MEDIA_PLANE

#include "tizentube_nx/playback/media_download.hpp"

#include <cstdio>
#include <cstdint>
#include <string_view>

namespace ttnx::switch_app {

// Fixed temporary movie cache only, never user save files or app config.
// This is intentionally only part of experimental M3 native media compilation.
// Content is published *only* after a successful bounded download.
class SwitchMediaCacheSink final : public playback::MediaChunkSink {
public:
    enum class Slot { MuxedVideo, AdaptiveVideo, AdaptiveAudio };
    explicit SwitchMediaCacheSink(Slot slot=Slot::MuxedVideo):slot_(slot) {}
    ~SwitchMediaCacheSink() override { abort(); }
    SwitchMediaCacheSink(const SwitchMediaCacheSink&) = delete;
    SwitchMediaCacheSink& operator=(const SwitchMediaCacheSink&) = delete;

    bool begin() override;
    bool append(std::string_view data) override;
    bool finish() override;
    void abort() noexcept override;

#ifdef TTNX_M3_CACHE_HOST_TEST
    static constexpr const char* kFolder="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache";
    static constexpr const char* kComplete="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/active.mp4";
    static constexpr const char* kPartial="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/active.mp4.part";
    static constexpr const char* kAdaptiveVideo="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_video.mp4";
    static constexpr const char* kAdaptiveAudio="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_audio.m4a";
    static constexpr const char* kAdaptiveMarker="/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/media-cache/adaptive_verified.txt";
#else
    static constexpr const char* kFolder="sdmc:/switch/TizenTube-NX/media-cache";
    static constexpr const char* kComplete="sdmc:/switch/TizenTube-NX/media-cache/active.mp4";
    static constexpr const char* kPartial="sdmc:/switch/TizenTube-NX/media-cache/active.mp4.part";
    static constexpr const char* kAdaptiveVideo="sdmc:/switch/TizenTube-NX/media-cache/adaptive_video.mp4";
    static constexpr const char* kAdaptiveAudio="sdmc:/switch/TizenTube-NX/media-cache/adaptive_audio.m4a";
    static constexpr const char* kAdaptiveMarker="sdmc:/switch/TizenTube-NX/media-cache/adaptive_verified.txt";
#endif
    // Multi-file pair is published only after BOTH downloads completed and
    // each MP4 box structure passed. Never open a partial .part pair.
    static void invalidate_adaptive_pair() noexcept;
    [[nodiscard]] static bool publish_adaptive_pair() noexcept;
    [[nodiscard]] static bool adaptive_pair_valid() noexcept;

private:
    [[nodiscard]] const char* partial() const noexcept;
    [[nodiscard]] const char* complete() const noexcept;
    Slot slot_{Slot::MuxedVideo};
    std::FILE* file_{nullptr};
    std::uint64_t size_{0};
};

} // namespace ttnx::switch_app

#endif // TTNX_M3_MEDIA_PLANE
