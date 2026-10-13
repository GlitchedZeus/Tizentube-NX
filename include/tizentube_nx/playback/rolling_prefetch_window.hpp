// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ttnx::playback {
// Producer-only admission logic. Does not own libmpv or any media bytes.
// Durations come from the independently validated VOD HLS playlist.
class RollingPrefetchWindow final {
public:
    static constexpr std::uint64_t kMaxAheadMs=15000;
    static constexpr std::uint64_t kMaxTotalMs=12ULL*60*60*1000;
    [[nodiscard]] bool configure(const std::vector<std::uint64_t>& durations_ms) {
        if(durations_ms.empty() || durations_ms.size()>4096)return false;
        std::vector<std::uint64_t> starts;
        starts.reserve(durations_ms.size());
        std::uint64_t elapsed=0;
        for(const auto duration:durations_ms) {
            if(duration==0 || duration>60000 || duration>kMaxTotalMs-elapsed)
                return false;
            starts.push_back(elapsed);
            elapsed+=duration;
        }
        starts_=std::move(starts);
        duration_ms_=elapsed;
        return true;
    }
    [[nodiscard]] bool can_fetch(std::size_t index,std::size_t epoch_origin,
                                 std::uint64_t playback_ms) const noexcept {
        if(starts_.empty())return true; // legacy host fixture, NOT native plan
        if(index>=starts_.size() || epoch_origin>=starts_.size() ||
           index<epoch_origin)return false;
        const auto relative=starts_[index]-starts_[epoch_origin];
        // Avoid overflow with subtractive bounds, not playhead+kMaxAheadMs.
        return relative<=playback_ms || relative-playback_ms<=kMaxAheadMs;
    }
    [[nodiscard]] std::size_t count()const noexcept{return starts_.size();}
    [[nodiscard]] std::uint64_t total_duration_ms()const noexcept{return duration_ms_;}
    [[nodiscard]] bool configured()const noexcept{return !starts_.empty();}
private:
    std::vector<std::uint64_t> starts_{};
    std::uint64_t duration_ms_{0};
};
} // namespace ttnx::playback
