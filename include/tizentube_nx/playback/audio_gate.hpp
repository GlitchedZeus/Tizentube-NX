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
        begun_=monotonic_ms;
        state_=State::Checking;
    }
    void reset() noexcept {
        state_=State::Idle;
        begun_=0;
    }
    [[nodiscard]] State state() const noexcept {return state_;}
    [[nodiscard]] State observe(bool audio_selected,
                                std::uint64_t monotonic_ms) noexcept {
        if(state_!=State::Checking)return state_;
        if(audio_selected) {
            state_=State::Ready;
        } else {
            // A monotonic timer should not regress, but fail safely in case
            // of a clock epoch reset rather than underflowing uint64.
            if(monotonic_ms<begun_)begun_=monotonic_ms;
            if(monotonic_ms-begun_>=kGraceMs)state_=State::Missing;
        }
        return state_;
    }
private:
    State state_{State::Idle};
    std::uint64_t begun_{0};
};
} // namespace ttnx::playback
