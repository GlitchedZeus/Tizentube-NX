#pragma once
#include <atomic>

namespace ttnx::playback {
// Protects the app's stable "ready/" directory while libmpv may have
// open media FDs. The downloader may stage "pending/", but cannot swap
// the currently playing directory until the player fully shuts down.
class HlsPlaybackLease {
public:
    HlsPlaybackLease()=default;
    HlsPlaybackLease(const HlsPlaybackLease&)=delete;
    HlsPlaybackLease& operator=(const HlsPlaybackLease&)=delete;
    ~HlsPlaybackLease(){release();}
    [[nodiscard]] bool acquire() noexcept {
        if(owned_)return true;
        bool free=false;
        owned_=active_.compare_exchange_strong(
            free,true,std::memory_order_acq_rel);
        return owned_;
    }
    void release() noexcept {
        if(owned_) {
            active_.store(false,std::memory_order_release);
            owned_=false;
        }
    }
    [[nodiscard]] static bool is_active() noexcept {
        return active_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool owned() const noexcept {return owned_;}
private:
    static inline std::atomic<bool> active_{false};
    bool owned_{false};
};
} // namespace ttnx::playback
