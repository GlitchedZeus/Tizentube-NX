#pragma once

#include "tizentube_nx/ui/tv_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace ttnx::ui {

// UI-only reference geometry extracted from user-captured 1280x720 Switch
// playback screenshots, not a replacement of the known-good libmpv renderer.
// Everything is app-owned original numeric layout, not Google assets/code.
struct TvPlayerLayout final {
    static constexpr float kCanvasWidth = 1280.0f;
    static constexpr float kCanvasHeight = 720.0f;
    static constexpr float kDetailsLeft = 64.0f;
    static constexpr float kDetailsTop = 68.0f;
    static constexpr float kDetailsWidth = 760.0f;
    static constexpr float kSeekX = 64.0f;
    static constexpr float kSeekY = 474.0f;
    static constexpr float kSeekWidth = 1152.0f;
    static constexpr float kSeekTouchHeight = 30.0f;
    static constexpr float kPlaybackControlsY = 527.0f;
    static constexpr float kRelatedTop = 598.0f;
    static constexpr float kRelatedCardWidth = 368.0f;
    static constexpr float kRelatedCardHeight = 207.0f;
    static constexpr float kRelatedCardGap = 24.0f;
    static constexpr float kPanelX = 836.0f;
    static constexpr float kPanelTop = 50.0f;
    static constexpr float kPanelWidth = 408.0f;
    static constexpr float kPanelRowHeight = 64.0f;
    static constexpr float kPanelRowGap = 8.0f;
    static constexpr float kVideoInsetX = 36.0f;
    static constexpr float kVideoInsetMaxWidth = 764.0f;
    static constexpr float kVideoInsetCenterY = 360.0f;
    static constexpr float kPanelFocusRadius = 14.0f;
    static constexpr float kPlayerFocusThickness = 5.0f;

    enum class Mode : std::uint8_t { Fullscreen, Settings };
    [[nodiscard]] static constexpr TvRect media_view(Mode mode, float factor = 1.0f) noexcept {
        if (factor <= 0.0f) return {};
        if (mode == Mode::Fullscreen)
            return {0.0f, 0.0f, kCanvasWidth * factor, kCanvasHeight * factor};
        // 16:9 inset stays centered on the left side while settings are open.
        constexpr float inset_height = kVideoInsetMaxWidth * 9.0f / 16.0f;
        return {kVideoInsetX * factor,
                (kVideoInsetCenterY - inset_height / 2.0f) * factor,
                kVideoInsetMaxWidth * factor, inset_height * factor};
    }
    [[nodiscard]] static constexpr TvRect details(float factor=1.0f) noexcept {
        return {kDetailsLeft*factor, kDetailsTop*factor,
                kDetailsWidth*factor, 110.0f*factor};
    }
    [[nodiscard]] static constexpr TvRect seek_hitbox(float factor=1.0f) noexcept {
        return {kSeekX*factor, (kSeekY-kSeekTouchHeight/2)*factor,
                kSeekWidth*factor, kSeekTouchHeight*factor};
    }
    [[nodiscard]] static constexpr TvRect panel_row(std::size_t index, float factor=1.0f) noexcept {
        return {kPanelX*factor,
                (kPanelTop + 30.0f + static_cast<float>(index)*(kPanelRowHeight+kPanelRowGap))*factor,
                kPanelWidth*factor, kPanelRowHeight*factor};
    }
    [[nodiscard]] static constexpr TvRect related_card(std::size_t index,
                                                       float scroll_x=0.0f,
                                                       float factor=1.0f) noexcept {
        return {(kDetailsLeft + static_cast<float>(index)*(kRelatedCardWidth+kRelatedCardGap))*factor - scroll_x,
                kRelatedTop*factor,kRelatedCardWidth*factor,kRelatedCardHeight*factor};
    }
    [[nodiscard]] static constexpr float clamped_seek_fraction(float playhead_s,
                                                               float duration_s) noexcept {
        if (!(duration_s > 0.0f) || !(playhead_s >= 0.0f)) return 0.0f;
        return std::clamp(playhead_s / duration_s, 0.0f, 1.0f);
    }
    // Style model, not a claim that each preference has a working backend.
    static constexpr std::array<std::string_view, 6> kCandidateSettings {{
        "Quality", "Captions", "Audio", "Speed", "Repeat", "Diagnostics"
    }};
    static constexpr std::array<std::uint16_t, 4> kCandidateQualityHeights {{
        720, 480, 360, 240
    }};
};

static_assert(TvPlayerLayout::kCandidateQualityHeights[0] == 720);
static_assert(TvPlayerLayout::kRelatedCardWidth*9.0f ==
              TvPlayerLayout::kRelatedCardHeight*16.0f);
static_assert(TvPlayerLayout::kPanelX + TvPlayerLayout::kPanelWidth < 1280);
} // namespace ttnx::ui
