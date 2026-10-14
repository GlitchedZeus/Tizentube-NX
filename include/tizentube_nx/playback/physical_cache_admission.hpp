#pragma once
#include <cstddef>
#include <cstdint>
namespace ttnx::playback {
// Pure physical SD admission decision. Called after verified HTTP Content-Length
// but before the first body byte, so declined requests cannot create media.
struct PhysicalCacheAdmission final {
    enum class Result : std::uint8_t { Accepted, Backpressure, Invalid };
    static constexpr std::uint64_t kMediaCapacity = 24ULL*1024*1024-131072;
    static constexpr std::uint64_t kMaxOneSegment = 16ULL*1024*1024;
    [[nodiscard]] static constexpr Result decide(std::uint64_t used,
                                                  std::uint64_t announced) noexcept {
        if(used>kMediaCapacity || announced<376 || announced>kMaxOneSegment)
            return Result::Invalid;
        return announced>kMediaCapacity-used ? Result::Backpressure : Result::Accepted;
    }
};
} // namespace ttnx::playback
