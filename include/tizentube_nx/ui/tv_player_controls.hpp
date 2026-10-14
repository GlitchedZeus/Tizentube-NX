#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace ttnx::ui {
// Controller-only presentation logic. Never owns the decoder, download job,
// media file descriptors, or a second frame buffer.
class TvPlayerControls final {
public:
    enum class Panel : std::uint8_t { Hidden, Transport, Diagnostics };
    static constexpr std::uint64_t kAutoHideMs=4800;
    void activate(std::uint64_t now) noexcept {
        panel_=Panel::Transport;
        touched_ms_=now;
    }
    void toggle(std::uint64_t now) noexcept {
        if(panel_==Panel::Hidden) activate(now);
        else if(panel_==Panel::Diagnostics) activate(now);
        else panel_=Panel::Hidden;
    }
    void diagnostics(std::uint64_t now) noexcept {
        panel_=Panel::Diagnostics;
        touched_ms_=now;
    }
    // Closing diagnostics reveals transport; closing transport clears video.
    [[nodiscard]] bool close_one(std::uint64_t now) noexcept {
        if(panel_==Panel::Hidden)return false;
        if(panel_==Panel::Diagnostics)activate(now);
        else panel_=Panel::Hidden;
        return true;
    }
    void activity(std::uint64_t now) noexcept {
        if(panel_!=Panel::Hidden)touched_ms_=now;
    }
    void tick(std::uint64_t now,bool paused) noexcept {
        if(panel_==Panel::Transport && !paused && now>=touched_ms_ &&
           now-touched_ms_>=kAutoHideMs)panel_=Panel::Hidden;
    }
    [[nodiscard]] Panel panel()const noexcept {return panel_;}
    [[nodiscard]] bool transport_visible()const noexcept {
        return panel_!=Panel::Hidden;
    }
    [[nodiscard]] static float progress(double time,double length) noexcept {
        if(!std::isfinite(time) || !std::isfinite(length) || length<=0)return 0;
        return static_cast<float>(std::clamp(time/length,0.0,1.0));
    }
    [[nodiscard]] static int64_t bounded_seek(int64_t current,int delta,
                                              int64_t duration) noexcept {
        if(current<0)current=0;
        const auto bounded=std::clamp<int64_t>(delta,-600,600);
        const auto result=bounded<0 && current < -bounded ? 0 : current+bounded;
        return duration>0?std::clamp<int64_t>(result,0,duration):std::max<int64_t>(0,result);
    }
private:
    Panel panel_{Panel::Transport};
    std::uint64_t touched_ms_{0};
};
} // namespace ttnx::ui
