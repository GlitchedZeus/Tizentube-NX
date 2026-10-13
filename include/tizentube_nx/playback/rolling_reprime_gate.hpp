// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace ttnx::playback {
// Pure decision model; the caller still owns mpv teardown, worker join,
// decoder-close acknowledgment and atomic SD deletion before restart_at().
// In particular, never infer that libmpv closed a reader from its playhead.
class RollingReprimeGate final {
public:
    enum class Stage : std::uint8_t {
        Watching, Requested, WaitingForPrefix, Finished, Cancelled, Failed
    };
    enum class End : std::uint8_t { Eof, Error, Stop };
    struct Boundary {
        std::size_t next_segment{};
        std::size_t epoch_origin{};
        std::size_t total_segments{};
        bool quota_eof{};
        bool decoder_attached{};
        bool complete_prefix{};
    };
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] std::size_t next_segment() const noexcept { return next_segment_; }
    [[nodiscard]] std::size_t starts_completed() const noexcept { return starts_completed_; }
    [[nodiscard]] bool observe(End why,const Boundary& b,bool verified_av) noexcept {
        if(stage_!=Stage::Watching)return false;
        if(why==End::Stop){cancel();return false;}
        if(why==End::Error){stage_=Stage::Failed;return false;}
        if(why!=End::Eof || !b.complete_prefix || !verified_av ||
           !b.decoder_attached || b.epoch_origin>=b.total_segments ||
           b.next_segment<=b.epoch_origin ||
           b.next_segment>b.total_segments ||
           b.next_segment<=last_started_) {
            stage_=Stage::Failed;
            return false;
        }
        if(!b.quota_eof) {
            // A genuinely complete stream is not a reason to re-open a CDN.
            stage_=b.next_segment==b.total_segments
                ?Stage::Finished:Stage::Failed;
            return false;
        }
        if(b.next_segment==b.total_segments){stage_=Stage::Finished;return false;}
        next_segment_=b.next_segment;
        stage_=Stage::Requested;
        return true;
    }
    [[nodiscard]] bool started(std::size_t segment) noexcept {
        if(stage_!=Stage::Requested || segment!=next_segment_ ||
           segment<=last_started_)return false;
        last_started_=segment;
        stage_=Stage::WaitingForPrefix;
        return true;
    }
    [[nodiscard]] bool prefix_ready() noexcept {
        if(stage_!=Stage::WaitingForPrefix)return false;
        ++starts_completed_;
        stage_=Stage::Watching;
        return true;
    }
    void fail() noexcept {if(stage_!=Stage::Cancelled)stage_=Stage::Failed;}
    void cancel() noexcept {stage_=Stage::Cancelled;}
private:
    Stage stage_{Stage::Watching};
    std::size_t next_segment_{0},last_started_{0},starts_completed_{0};
};
} // namespace ttnx::playback
