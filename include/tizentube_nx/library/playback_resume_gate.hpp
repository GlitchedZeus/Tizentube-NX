#pragma once
#include <algorithm>
#include <cstdint>
namespace ttnx::library {
// Independent from libmpv so replay correctness can be tested on host.
// A saved timestamp is never applied before picture + selected audio exist.
class PlaybackResumeGate final {
public:
    explicit PlaybackResumeGate(std::uint32_t target_seconds=0) noexcept
        :requested_(target_seconds){}
    [[nodiscard]] bool offer(bool picture,bool audio,bool rolling,
                             std::uint32_t duration,std::uint32_t& seek_seconds) noexcept {
        if(attempted_ || rolling || !picture || !audio || requested_<5)return false;
        attempted_=true;
        // Do not immediately land at the end of a completed video.
        const auto max=duration>=15?duration-5:0;
        effective_=max?std::min(requested_,max):requested_;
        if(effective_<5){effective_=0;return false;}
        seek_seconds=effective_;
        return true;
    }
    void acknowledge(bool successful) noexcept {success_=successful;}
    [[nodiscard]] bool can_checkpoint(std::uint32_t current)const noexcept {
        if(requested_<5)return true;
        if(!attempted_)return false;
        if(!success_)return false; // Preserve previous resume after failed seek
        // Seek is asynchronous. Do not replace a 10-minute checkpoint with
        // the first frame that appears at 0 while libmpv is still seeking.
        return current>=effective_ ||
               (effective_>=20 && current>=effective_-10);
    }
private:
    std::uint32_t requested_{0},effective_{0};
    bool attempted_{false},success_{false};
};
} // namespace ttnx::library
