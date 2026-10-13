// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace ttnx::playback {
// M4 uses real rendered-frame/audio-selected libmpv clock samples, never
// speculative UI seek positions. HLS gives the absolute start of each epoch.
// The initial mpv time-pos may be zero-relative OR original MPEG-TS PTS:
// anchor the first verified sample and admit only monotonic local progress.
class RollingEpochClock final {
public:
    static constexpr std::uint64_t kMaximumVodMs=12ULL*60*60*1000;
    [[nodiscard]] bool reset(std::uint64_t epoch_start_ms,
                             std::uint64_t duration_ms) noexcept {
        valid_=duration_ms>0 && duration_ms<=kMaximumVodMs &&
               epoch_start_ms<duration_ms;
        epoch_start_ms_=valid_?epoch_start_ms:0;
        duration_ms_=valid_?duration_ms:0;
        anchored_=false;
        raw_first_ms_=raw_last_ms_=relative_ms_=0;
        return valid_;
    }
    [[nodiscard]] bool observe(double mpv_seconds) noexcept {
        if(!valid_ || !std::isfinite(mpv_seconds) || mpv_seconds<0 ||
           mpv_seconds>static_cast<double>(kMaximumVodMs)/1000.0)
            return false;
        const auto raw=static_cast<std::uint64_t>(mpv_seconds*1000.0);
        if(!anchored_) {
            anchored_=true;
            raw_first_ms_=raw_last_ms_=raw;
            relative_ms_=0;
            return true;
        }
        if(raw<raw_last_ms_ || raw<raw_first_ms_ ||
           raw-raw_first_ms_>duration_ms_-epoch_start_ms_)return false;
        raw_last_ms_=raw;
        relative_ms_=raw-raw_first_ms_;
        return true;
    }
    [[nodiscard]] bool anchored() const noexcept {return anchored_;}
    [[nodiscard]] std::uint64_t relative_ms() const noexcept{return relative_ms_;}
    [[nodiscard]] std::uint64_t absolute_ms() const noexcept {
        return epoch_start_ms_+relative_ms_;
    }
private:
    std::uint64_t epoch_start_ms_{0},duration_ms_{0};
    std::uint64_t raw_first_ms_{0},raw_last_ms_{0},relative_ms_{0};
    bool valid_{false},anchored_{false};
};
} // namespace ttnx::playback
