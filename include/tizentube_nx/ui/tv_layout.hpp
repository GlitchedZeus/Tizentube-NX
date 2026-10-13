#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ttnx::ui {

// Original TizenTube NX layout tokens, derived from user-captured
// 1280x720 Switch YouTube TV screenshots (2026-10-12). These are
// measured APPROXIMATIONS for our own Borealis/NanoVG UI, not copied
// Google graphics, assets, or application code.
struct TvRect {
    float x{}, y{}, w{}, h{};
    [[nodiscard]] constexpr float right() const noexcept { return x + w; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + h; }
};

enum class TvRailMode : std::uint8_t { Collapsed, Expanded };

struct TvLayout final {
    static constexpr float kReferenceWidth = 1280.0f;
    static constexpr float kReferenceHeight = 720.0f;
    static constexpr float kCompactRailWidth = 88.0f;
    static constexpr float kExpandedRailWidth = 340.0f;
    static constexpr float kContentInset = 16.0f;
    static constexpr float kCardsTop = 102.0f;
    static constexpr float kCardWidth = 352.0f;
    static constexpr float kCardHeight = 198.0f; // exact 16:9 reference
    static constexpr float kCardGap = 24.0f;
    static constexpr float kFocusThickness = 5.0f;
    static constexpr float kFocusRadius = 14.0f;
    static constexpr float kAlbumWidth = 200.0f;
    static constexpr float kAlbumGap = 24.0f;
    static constexpr float kSettingsMenuLeft = 98.0f;
    static constexpr float kSettingsMenuWidth = 280.0f;
    static constexpr float kSettingsDividerX = 393.0f;
    static constexpr float kSettingsDetailLeft = 470.0f;

    [[nodiscard]] static constexpr float scale(float width, float height) noexcept {
        if (width <= 0 || height <= 0) return 0.0f;
        return std::min(width / kReferenceWidth, height / kReferenceHeight);
    }
    [[nodiscard]] static constexpr float rail_width(TvRailMode mode, float scale_factor = 1.0f) noexcept {
        return (mode == TvRailMode::Expanded ? kExpandedRailWidth : kCompactRailWidth) * scale_factor;
    }
    [[nodiscard]] static constexpr float content_left(TvRailMode mode, float scale_factor = 1.0f) noexcept {
        return rail_width(mode, scale_factor) + kContentInset * scale_factor;
    }
    [[nodiscard]] static constexpr TvRect card(std::size_t index,
                                               TvRailMode mode,
                                               float scroll_x = 0.0f,
                                               float scale_factor = 1.0f) noexcept {
        return {content_left(mode, scale_factor) +
                    static_cast<float>(index) * (kCardWidth + kCardGap) * scale_factor - scroll_x,
                kCardsTop * scale_factor, kCardWidth * scale_factor, kCardHeight * scale_factor};
    }
    [[nodiscard]] static constexpr TvRect album(std::size_t index,
                                                TvRailMode mode,
                                                float scroll_x = 0.0f,
                                                float scale_factor = 1.0f) noexcept {
        return {content_left(mode, scale_factor) +
                    static_cast<float>(index) * (kAlbumWidth + kAlbumGap) * scale_factor - scroll_x,
                kCardsTop * scale_factor, kAlbumWidth * scale_factor, kAlbumWidth * scale_factor};
    }
    // Camera/thumbnail browsing: move focus into view, never scroll so far
    // right that the end of a row leaves a completely empty canvas.
    [[nodiscard]] static constexpr float scroll_to_reveal(std::size_t focus_index,
                                                           std::size_t count,
                                                           TvRailMode mode,
                                                           float current_scroll,
                                                           float canvas_width = kReferenceWidth,
                                                           float scale_factor = 1.0f) noexcept {
        if (!count || focus_index >= count || canvas_width <= 0 || scale_factor <= 0) return 0.0f;
        const float left = content_left(mode, scale_factor);
        const float visible_right = canvas_width - kContentInset * scale_factor;
        const float card_step = (kCardWidth + kCardGap) * scale_factor;
        const float end = left + (static_cast<float>(count - 1) * card_step)
                            + kCardWidth * scale_factor;
        const float max_scroll = std::max(0.0f, end - visible_right);
        const float bounded_scroll = std::clamp(current_scroll, 0.0f, max_scroll);
        const float start = left + static_cast<float>(focus_index) * card_step;
        const float finish = start + kCardWidth * scale_factor;
        if (start - bounded_scroll < left)
            return std::clamp(start - left, 0.0f, max_scroll);
        if (finish - bounded_scroll > visible_right)
            return std::clamp(finish - visible_right, 0.0f, max_scroll);
        return bounded_scroll;
    }
};

static_assert(TvLayout::kCardHeight * 16 == TvLayout::kCardWidth * 9);
static_assert(TvLayout::content_left(TvRailMode::Expanded) == 356.0f);
static_assert(TvLayout::content_left(TvRailMode::Collapsed) == 104.0f);

} // namespace ttnx::ui
