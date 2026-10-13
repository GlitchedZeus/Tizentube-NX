#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace ttnx::switch_app {
// Experimental native HLS rolling backend: only locally named, validated
// MPEG-TS segments. The current accepted libmpv player never opens this path.
class RollingHlsSink final {
public:
#ifdef TTNX_M4_ROLLING_HOST_TEST
    static constexpr const char* kRoot = "/tmp/ttnx-m4-rolling-test/switch/TizenTube-NX/media-cache/rolling";
#else
    static constexpr const char* kRoot = "sdmc:/switch/TizenTube-NX/media-cache/rolling";
#endif
#ifdef TTNX_M4_ROLLING_HOST_TEST
    static constexpr const char* kPlaylist =
        "/tmp/ttnx-m4-rolling-test/switch/TizenTube-NX/media-cache/rolling/stream.m3u8";
#else
    static constexpr const char* kPlaylist =
        "sdmc:/switch/TizenTube-NX/media-cache/rolling/stream.m3u8";
#endif
    static constexpr std::uint64_t kMaxMediaBytes=24ULL*1024*1024-131072;
    static constexpr std::size_t kMaxSegments=4096;
    ~RollingHlsSink() = default;
#ifdef TTNX_M4_ROLLING_HOST_TEST
    // Deterministic FAT/non-overwrite promotion failure simulation.
    static void test_fail_manifest_promotion(bool failure) noexcept {
        test_promotion_failure_=failure;
    }
#endif
    [[nodiscard]] bool begin(std::uint64_t maximum_segment_ms=6000);
    [[nodiscard]] bool append(std::size_t index,std::uint64_t duration_ms,
                              std::string_view verified_muxed_ts);
    // M4 low-RAM producer API. Invoke emit() repeatedly with <=32 KiB of
    // approved HTTPS media; no contiguous whole-segment buffer is needed.
    // All chunks remain in a private .part until PAT/PMT, transport sync,
    // quota, fsync and playlist publication succeed. Never pass signed URLs.
    using ChunkEmitter=std::function<bool(std::string_view)>;
    using ChunkProducer=std::function<bool(const ChunkEmitter&)>;
    [[nodiscard]] bool append_streamed(std::size_t index,std::uint64_t duration_ms,
                                       const ChunkProducer& producer);
    // Publish a moving, local-only live playlist. The caller must have
    // released the decoder's use of all segments below first_index.
    [[nodiscard]] bool retire_before(std::size_t first_index);
    [[nodiscard]] bool finish();
    // Caller must close libmpv and join workers before discarding media.
    [[nodiscard]] static bool discard() noexcept;
    // Clear this instance only after every tracked media file is safely
    // removed and the player has released the global HLS decoder lease.
    [[nodiscard]] bool close_session() noexcept;
    [[nodiscard]] std::uint64_t bytes() const noexcept {return bytes_;}
    [[nodiscard]] std::size_t active_segments() const noexcept {return segments_.size();}
    [[nodiscard]] std::size_t next_index() const noexcept {return next_;}
private:
    struct Segment {std::size_t index;std::uint64_t duration_ms,bytes;};
    [[nodiscard]] bool publish(bool end_list);
    std::vector<Segment> segments_{};
    std::uint64_t bytes_{0};
    std::size_t next_{0};
    std::uint64_t target_seconds_{0};
    bool started_{false};
    bool finished_{false};
#ifdef TTNX_M4_ROLLING_HOST_TEST
    static inline bool test_promotion_failure_{false};
#endif
};
} // namespace ttnx::switch_app
