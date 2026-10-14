#pragma once
#include <algorithm>
namespace ttnx::playback {
// mpv software-volume percentages; 100 is the unamplified source.
// Modest default boost requested by hardware QA; cap user gain to reduce
// distortion risk. Neither output volume nor playback preferences persist.
class PlayerVolume {
public:
    static constexpr int kDefault=130;
    static constexpr int kMinimum=0;
    static constexpr int kMaximum=150;
    static constexpr int kStep=10;
    [[nodiscard]] constexpr int percent()const noexcept {return percent_;}
    [[nodiscard]] constexpr bool adjust(int delta) noexcept {
        if(delta!=kStep && delta!=-kStep)return false;
        const int next=std::clamp(percent_+delta,kMinimum,kMaximum);
        if(next==percent_)return false;
        percent_=next;
        return true;
    }
    [[nodiscard]] constexpr bool set(int volume)noexcept {
        if(volume<kMinimum || volume>kMaximum)return false;
        percent_=volume;return true;
    }
private:
    int percent_{kDefault};
};
} // namespace ttnx::playback
