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
    // Numeric codes are written to M3 field log; no file names, URLs,
    // account data or arbitrary libc text ever leave the SD cache layer.
    enum class StorageStep : int {
        None=0, Open=1, Write=2, Flush=3, Sync=4, Close=5,
        Size=6, Backup=7, Promote=8, Restore=9
    };
    explicit SwitchMediaCacheSink(Slot slot=Slot::MuxedVideo):slot_(slot) {}
    ~SwitchMediaCacheSink() override { abort(); }
    SwitchMediaCacheSink(const SwitchMediaCacheSink&) = delete;
    SwitchMediaCacheSink& operator=(const SwitchMediaCacheSink&) = delete;

    bool begin() override;
    bool append(std::string_view data) override;
    bool finish() override;
    void abort() noexcept override;
    [[nodiscard]] StorageStep last_storage_error() const noexcept {
        return last_error_step_;
    }
    [[nodiscard]] int last_storage_errno() const noexcept {
        return last_error_errno_;
    }
#ifdef TTNX_M3_CACHE_HOST_TEST
    // Deterministic fault injection for exact SD finalization stages.
    static void test_fail_on(StorageStep stage) noexcept {test_failure_=stage;}
#endif

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
    // Session-only media. Fail closed if an active player owns its file
    // descriptors. Delete only a fixed whitelist of our own temporary
    // video/audio/marker files (including crashed .part/.previous files).
    [[nodiscard]] static bool discard_session_media() noexcept;
    static void invalidate_adaptive_pair() noexcept;
    [[nodiscard]] static bool publish_adaptive_pair() noexcept;
    [[nodiscard]] static bool adaptive_pair_valid() noexcept;

private:
    [[nodiscard]] const char* partial() const noexcept;
    [[nodiscard]] const char* complete() const noexcept;
    [[nodiscard]] const char* backup() const noexcept;
    void record_failure(StorageStep stage,int error_code) noexcept {
        last_error_step_=stage;
        last_error_errno_=error_code;
    }
#ifdef TTNX_M3_CACHE_HOST_TEST
    static inline StorageStep test_failure_{StorageStep::None};
#endif
    Slot slot_{Slot::MuxedVideo};
    StorageStep last_error_step_{StorageStep::None};
    int last_error_errno_{0};
    std::FILE* file_{nullptr};
    std::uint64_t size_{0};
};

} // namespace ttnx::switch_app

#endif // TTNX_M3_MEDIA_PLANE
