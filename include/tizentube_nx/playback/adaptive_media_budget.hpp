#pragma once
#include <atomic>
#include <cstdint>
namespace ttnx::playback {
// M3 is a full-file-only path. Reserve space for the NRO and app metadata.
// A long VOD must use a future bounded streaming transport, not a giant SD
// download. Both independent AAC and H264 workers admit their declared size.
class AdaptiveMediaBudget {
public:
    static constexpr std::uint64_t kMaxCompletedPair=60000000ULL;
    [[nodiscard]] bool admit(std::uint64_t size) noexcept {
        if(!size||size>kMaxCompletedPair)return false;
        auto current=reserved_.load(std::memory_order_acquire);
        for(;;) {
            if(current>kMaxCompletedPair-size)return false;
            if(reserved_.compare_exchange_weak(current,current+size,
                std::memory_order_acq_rel,std::memory_order_acquire))return true;
        }
    }
    [[nodiscard]] std::uint64_t reserved()const noexcept {
        return reserved_.load(std::memory_order_acquire);
    }
private:
    std::atomic<std::uint64_t> reserved_{0};
};
} // namespace ttnx::playback
