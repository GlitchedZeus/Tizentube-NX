#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>

namespace ttnx::playback {

// Product-wide accounting target: NRO + assets, configuration/diagnostics,
// and the high-water reservation for a future rolling AV buffer.
// MB here is decimal (100,000,000 bytes), not 100 MiB.
struct StorageBudget final {
    static constexpr std::uint64_t kTotalAppBytes = 100'000'000;
    static constexpr std::uint64_t kReservedPersistentBytes = 8ULL * 1024 * 1024;
    static constexpr std::uint64_t kRollingMediaBytes = 24ULL * 1024 * 1024;
    static constexpr std::uint64_t kMaxNroAndAssetsBytes =
        kTotalAppBytes - kReservedPersistentBytes - kRollingMediaBytes;

    // NRO size must be measured by the packaging workflow. Runtime app-owned
    // state must remain in its reserved slice; not all log files are media.
    static constexpr bool fits(std::uint64_t packaged_bytes,
                               std::uint64_t persistent_bytes,
                               std::uint64_t media_bytes) noexcept {
        return packaged_bytes <= kMaxNroAndAssetsBytes &&
               persistent_bytes <= kReservedPersistentBytes &&
               media_bytes <= kRollingMediaBytes &&
               packaged_bytes + persistent_bytes + media_bytes <= kTotalAppBytes;
    }
};

// This is a pure, single-owner admission/eviction scheduler, NOT a usable
// playback transport. It never performs SD I/O or opens an unverified stream.
// A later native streaming lane must integrate it with actual network back-
// pressure, decoder leases, validated manifests and seek/cancel behavior.
class RollingMediaBudget final {
public:
    enum class Track : std::uint8_t { Video, Audio, Muxed };
    enum class AddResult : std::uint8_t {
        Accepted, Invalid, OutOfOrder, AtCapacity, TooManyPieces
    };
    struct Piece {
        Track track;
        std::uint32_t index;
        std::uint64_t start_ms;
        std::uint64_t end_ms;
        std::uint64_t bytes;
        bool pinned;
    };

    static constexpr std::uint64_t kCapacityBytes = StorageBudget::kRollingMediaBytes;
    static constexpr std::uint64_t kStartupVideoMs = 3000;
    static constexpr std::uint64_t kStartupAudioMs = 3000;
    static constexpr std::uint64_t kEvictionGuardMs = 2000;
    static constexpr std::size_t kMaxPieces = 192;

    [[nodiscard]] AddResult add(Track track, std::uint32_t index,
                                std::uint64_t start_ms, std::uint64_t end_ms,
                                std::uint64_t bytes, bool pinned = false) {
        if (end_ms <= start_ms || end_ms - start_ms > 60000 ||
            !bytes || bytes > kCapacityBytes || index >= 4096)
            return AddResult::Invalid;
        if (pieces_.size() == kMaxPieces) return AddResult::TooManyPieces;
        if (bytes > kCapacityBytes - used_bytes_) return AddResult::AtCapacity;
        // For each elementary track require exactly increasing piece IDs
        // and a non-overlapping timeline. A seek creates a fresh epoch.
        // Preserve the highest sequence/time even when a played piece was
        // evicted: an expired signed URL or retry cannot reinsert old media.
        auto& previous = track == Track::Video ? last_video_ :
                         track == Track::Audio ? last_audio_ : last_muxed_;
        if (previous.valid &&
            (index <= previous.index || start_ms < previous.end_ms))
            return AddResult::OutOfOrder;
        // No media bytes are allocated here; the caller must complete the
        // verified SD/memory publication before advertising a piece.
        pieces_.push_back(Piece{track,index,start_ms,end_ms,bytes,pinned});
        previous = LastPiece{true,index,end_ms};
        used_bytes_ += bytes;
        high_water_bytes_ = std::max(high_water_bytes_,used_bytes_);
        return AddResult::Accepted;
    }

    [[nodiscard]] bool pin(Track track, std::uint32_t index,
                           bool pinned) noexcept {
        for (auto& piece : pieces_) {
            if (piece.track == track && piece.index == index) {
                piece.pinned = pinned;
                return true;
            }
        }
        return false;
    }

    // Return releasable byte count. Actual unlinking must succeed before a
    // caller makes freed quota available; therefore this only marks the
    // candidate and does not erase it. commit_eviction() follows SD unlink.
    [[nodiscard]] std::uint64_t evictable_bytes(std::uint64_t played_ms) const noexcept {
        if (played_ms <= kEvictionGuardMs) return 0;
        const auto before_ms = played_ms - kEvictionGuardMs;
        std::uint64_t total = 0;
        for (const auto& piece : pieces_)
            if (!piece.pinned && piece.end_ms <= before_ms)
                total += piece.bytes;
        return total;
    }

    [[nodiscard]] bool commit_eviction(Track track, std::uint32_t index,
                                       std::uint64_t played_ms) noexcept {
        if (played_ms <= kEvictionGuardMs) return false;
        for (auto it = pieces_.begin(); it != pieces_.end(); ++it) {
            if (it->track == track && it->index == index) {
                if (it->pinned || it->end_ms > played_ms - kEvictionGuardMs)
                    return false;
                used_bytes_ -= it->bytes;
                pieces_.erase(it);
                return true;
            }
        }
        return false;
    }

    // Safety-first: a seek/cancel must not clear the accounting of media
    // still held by libmpv; the caller closes/unpins decoder leases first.
    [[nodiscard]] bool clear_epoch() noexcept {
        for (const auto& p : pieces_) if (p.pinned) return false;
        pieces_.clear();
        used_bytes_ = 0;
        last_video_ = last_audio_ = last_muxed_ = {};
        verified_video_ = verified_audio_ = false;
        return true;
    }

    void set_codec_verification(bool video, bool audio) noexcept {
        verified_video_ = video;
        verified_audio_ = audio;
    }

    // Contiguous buffered coverage must begin at t=0 for both A/V. Presence
    // of a byte or a header alone is not evidence of decodable frames.
    [[nodiscard]] bool startup_ready() const noexcept {
        return verified_video_ && verified_audio_ &&
            contiguous_ms(Track::Video) >= kStartupVideoMs &&
            contiguous_ms(Track::Audio) >= kStartupAudioMs;
    }

    [[nodiscard]] std::uint64_t buffered_video_ms() const noexcept {
        return contiguous_ms(Track::Video);
    }
    [[nodiscard]] std::uint64_t buffered_audio_ms() const noexcept {
        return contiguous_ms(Track::Audio);
    }
    [[nodiscard]] std::uint64_t used_bytes() const noexcept { return used_bytes_; }
    [[nodiscard]] std::uint64_t high_water_bytes() const noexcept { return high_water_bytes_; }
    [[nodiscard]] std::size_t pieces() const noexcept { return pieces_.size(); }

private:
    struct LastPiece {
        bool valid{false};
        std::uint32_t index{0};
        std::uint64_t end_ms{0};
    };
    LastPiece last_video_{},last_audio_{},last_muxed_{};
    [[nodiscard]] std::uint64_t contiguous_ms(Track track) const noexcept {
        std::uint64_t end = 0;
        // A muxed stream counts toward both tracks, but only after its AAC
        // and H.264 structures have been separately verified by the caller.
        for (std::size_t step = 0; step < pieces_.size(); ++step) {
            std::uint64_t next = end;
            for (const auto& piece : pieces_)
                if ((piece.track == track || piece.track == Track::Muxed) &&
                    piece.start_ms <= end && piece.end_ms > next)
                    next = piece.end_ms;
            if (next == end) break;
            end = next;
        }
        return end;
    }
    std::deque<Piece> pieces_{};
    std::uint64_t used_bytes_{0};
    std::uint64_t high_water_bytes_{0};
    bool verified_video_{false}, verified_audio_{false};
};

} // namespace ttnx::playback
