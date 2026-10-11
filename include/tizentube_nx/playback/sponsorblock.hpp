#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ttnx::playback {

// SponsorBlock is separate from platform ad suppression. Network fetching and
// JSON parsing must feed normalized, bounded segments into this engine.
enum class SegmentCategory {
    Sponsor, SelfPromotion, Interaction, Intro, Outro, Preview, MusicOfftopic, Filler
};
enum class SkipMode { Disabled, Ask, Auto };
enum class SkipActionType { None, Offer, Skip };

struct Segment {
    SegmentCategory category{SegmentCategory::Sponsor};
    std::uint64_t start_ms{0};
    std::uint64_t end_ms{0};
};

struct SkipAction {
    SkipActionType type{SkipActionType::None};
    SegmentCategory category{SegmentCategory::Sponsor};
    std::uint64_t end_ms{0};
};

class SponsorBlockEngine {
public:
    void set_mode(SegmentCategory category, SkipMode mode);
    [[nodiscard]] SkipMode mode(SegmentCategory category) const;
    // Invalid, zero-length, out-of-duration and excessive segments are
    // dropped; same-category touching/overlapping ranges are merged.
    void set_segments(std::vector<Segment> segments, std::uint64_t duration_ms);
    [[nodiscard]] const std::vector<Segment>& segments() const noexcept {
        return segments_;
    }

    // Pure in-video decision. No network access and no direct player mutation.
    [[nodiscard]] SkipAction action_at(std::uint64_t position_ms);
    // Suppress repeated actions for the same segment while inside its range.
    void mark_handled(const SkipAction& action);
    void reset() noexcept;
private:
    [[nodiscard]] static unsigned index(SegmentCategory category);
    SkipMode modes_[8]{};
    std::vector<Segment> segments_;
    std::optional<Segment> handled_;
};

} // namespace ttnx::playback
