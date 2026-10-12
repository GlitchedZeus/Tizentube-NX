#pragma once
#include <cstddef>
#include <cstdint>
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
    static constexpr std::uint64_t kMaxMediaBytes=24ULL*1024*1024-65536;
    static constexpr std::size_t kMaxSegments=4096;
    ~RollingHlsSink() = default;
    [[nodiscard]] bool begin();
    [[nodiscard]] bool append(std::size_t index,std::uint64_t duration_ms,
                              std::string_view verified_muxed_ts);
    // Publish a moving, local-only live playlist. The caller must have
    // released the decoder's use of all segments below first_index.
    [[nodiscard]] bool retire_before(std::size_t first_index);
    [[nodiscard]] bool finish();
    // Caller must close libmpv and join workers before discarding media.
    [[nodiscard]] static bool discard() noexcept;
    [[nodiscard]] std::uint64_t bytes() const noexcept {return bytes_;}
    [[nodiscard]] std::size_t active_segments() const noexcept {return segments_.size();}
    [[nodiscard]] std::size_t next_index() const noexcept {return next_;}
private:
    struct Segment {std::size_t index;std::uint64_t duration_ms,bytes;};
    [[nodiscard]] bool publish(bool end_list);
    std::vector<Segment> segments_{};
    std::uint64_t bytes_{0};
    std::size_t next_{0};
    bool started_{false};
};
} // namespace ttnx::switch_app
