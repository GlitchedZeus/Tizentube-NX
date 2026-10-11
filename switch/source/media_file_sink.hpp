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
    SwitchMediaCacheSink() = default;
    ~SwitchMediaCacheSink() override { abort(); }
    SwitchMediaCacheSink(const SwitchMediaCacheSink&) = delete;
    SwitchMediaCacheSink& operator=(const SwitchMediaCacheSink&) = delete;

    bool begin() override;
    bool append(std::string_view data) override;
    bool finish() override;
    void abort() noexcept override;

    static constexpr const char* kComplete =
        "sdmc:/switch/TizenTube-NX/media-cache/active.mp4";
    static constexpr const char* kPartial =
        "sdmc:/switch/TizenTube-NX/media-cache/active.mp4.part";

private:
    std::FILE* file_{nullptr};
    std::uint64_t size_{0};
};

} // namespace ttnx::switch_app

#endif // TTNX_M3_MEDIA_PLANE
