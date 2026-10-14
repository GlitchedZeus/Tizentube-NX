#include "tizentube_nx/playback/sponsorblock.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace ttnx::playback {

unsigned SponsorBlockEngine::index(SegmentCategory category) {
    const auto i = static_cast<unsigned>(category);
    return i < 8 ? i : 8;
}

void SponsorBlockEngine::set_mode(SegmentCategory category, SkipMode mode) {
    const auto i = index(category);
    if (i < 8) modes_[i] = mode;
}

SkipMode SponsorBlockEngine::mode(SegmentCategory category) const {
    const auto i = index(category);
    return i < 8 ? modes_[i] : SkipMode::Disabled;
}

void SponsorBlockEngine::set_segments(
    std::vector<Segment> segments, std::uint64_t duration_ms) {
    reset();
    segments_.clear();
    // Malicious remote responses may carry excessive arrays. Reject rather
    // than sorting unbounded attacker-supplied content.
    if (segments.size() > 2048 || duration_ms == 0) return;
    segments.erase(std::remove_if(segments.begin(), segments.end(),
        [duration_ms](const Segment& s) {
            return index(s.category) >= 8 || s.start_ms >= s.end_ms ||
                   s.start_ms >= duration_ms || s.end_ms > duration_ms;
        }), segments.end());

    std::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
        if (a.category != b.category)
            return index(a.category) < index(b.category);
        if (a.start_ms != b.start_ms) return a.start_ms < b.start_ms;
        return a.end_ms < b.end_ms;
    });
    for (const auto& s : segments) {
        if (!segments_.empty() && segments_.back().category == s.category &&
            s.start_ms <= segments_.back().end_ms) {
            segments_.back().end_ms = std::max(segments_.back().end_ms, s.end_ms);
        } else {
            segments_.push_back(s);
        }
    }
    std::sort(segments_.begin(), segments_.end(), [](const Segment& a, const Segment& b) {
        if (a.start_ms != b.start_ms) return a.start_ms < b.start_ms;
        if (a.end_ms != b.end_ms) return a.end_ms > b.end_ms;
        return index(a.category) < index(b.category);
    });
}

SkipAction SponsorBlockEngine::action_at(std::uint64_t position_ms) {
    // Remove the latch when the player leaves the segment, permitting a
    // deliberate rewind back into a previously skipped section.
    if (handled_ && (position_ms < handled_->start_ms ||
                     position_ms >= handled_->end_ms)) handled_.reset();

    SkipAction best;
    for (const auto& s : segments_) {
        if (s.start_ms > position_ms) break;
        if (position_ms >= s.end_ms || mode(s.category) == SkipMode::Disabled)
            continue;
        if (handled_ && handled_->category == s.category &&
            handled_->start_ms == s.start_ms &&
            handled_->end_ms == s.end_ms) continue;

        const auto type = mode(s.category) == SkipMode::Auto
            ? SkipActionType::Skip : SkipActionType::Offer;
        // Auto wins over Ask; for equal mode choose furthest end to prevent
        // a second action immediately after an overlapping skip.
        if (best.type == SkipActionType::None ||
            (type == SkipActionType::Skip && best.type == SkipActionType::Offer) ||
            (type == best.type && s.end_ms > best.end_ms)) {
            best = {type, s.category, s.end_ms};
        }
    }
    return best;
}

void SponsorBlockEngine::mark_handled(const SkipAction& action) {
    if (action.type == SkipActionType::None) return;
    for (const auto& s : segments_) {
        if (s.category == action.category && s.end_ms == action.end_ms) {
            handled_ = s;
            return;
        }
    }
}

void SponsorBlockEngine::reset() noexcept {
    handled_.reset();
}

} // namespace ttnx::playback
