#pragma once
#include <cstdint>

namespace ttnx::playback {

// Time-bounded readiness contract independent of libmpv/console hardware.
// A playback-restart event is NOT evidence that asynchronous AAC selection
// has finished; an unselected track is an error only after a grace window.
class AudioSelectionGate {
public:
    enum class State { Idle, Checking, Ready, Missing };
    static constexpr std::uint64_t kGraceMs=4000;

    void begin(std::uint64_t monotonic_ms) noexcept {
        last_tick_=monotonic_ms;
        active_ms_=0;
        state_=State::Checking;
    }
    void reset() noexcept {
        state_=State::Idle;
        last_tick_=0;
        active_ms_=0;
    }
    [[nodiscard]] State state() const noexcept {return state_;}
    [[nodiscard]] State observe(bool audio_selected,
                                std::uint64_t monotonic_ms) noexcept {
        if(state_!=State::Checking)return state_;
        if(audio_selected) {
            state_=State::Ready;
        } else {
            // Track ACTIVE UI time only. A Switch sleep/wake may move the
            // monotonic clock forward by hours without a single player tick.
            // Clamp each observation to 100ms so sleep never causes a false
            // immediate "missing AAC" error on wake.
            const auto delta=monotonic_ms>=last_tick_
                ?monotonic_ms-last_tick_:0;
            last_tick_=monotonic_ms;
            active_ms_+=delta<100?delta:100;
            if(active_ms_>=kGraceMs)state_=State::Missing;
        }
        return state_;
    }
private:
    State state_{State::Idle};
    std::uint64_t last_tick_{0};
    std::uint64_t active_ms_{0};
};
} // namespace ttnx::playback
