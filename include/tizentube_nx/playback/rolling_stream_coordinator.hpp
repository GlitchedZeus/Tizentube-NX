#pragma once
#include "tizentube_nx/playback/rolling_media_budget.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ttnx::playback {

// Network/decoder-neutral coordinator for the proposed native bounded HLS
// path. The caller must independently validate source, codecs, HTTPS, and
// durable segment publication. The accepted M3 player does not use it yet.
class RollingStreamCoordinator final {
public:
    using Track=RollingMediaBudget::Track;
    enum class State : std::uint8_t {
        Idle, Priming, Ready, Backpressure, Failed, Cancelled
    };
    enum class Admission : std::uint8_t {
        Accepted, Invalid, Busy, Backpressure, Inactive
    };
    struct Ticket {
        std::uint64_t request_id{0};
        Track track{Track::Video};
        std::uint32_t index{0};
        std::uint64_t start_ms{0};
        std::uint64_t end_ms{0};
        std::uint64_t reserved_bytes{0};
    };
    static constexpr std::uint64_t kMaxSegmentBytes=16ULL*1024*1024;
    static constexpr std::uint64_t kMaxAheadMs=30000;
    static constexpr unsigned kMaxHeight=720;
    static constexpr unsigned kMaxNativeFps=60;

    [[nodiscard]] bool begin(std::uint64_t position_ms,unsigned height,
                             unsigned native_fps) noexcept {
        if(height==0 || height>kMaxHeight || native_fps==0 ||
           native_fps>kMaxNativeFps || pending_ ||
           (state_!=State::Idle && state_!=State::Cancelled) ||
           !budget_.begin_epoch(position_ms)) return false;
        position_ms_=epoch_ms_=position_ms;
        height_=height;fps_=native_fps;
        state_=State::Priming;
        ++serial_;
        return true;
    }

    [[nodiscard]] Admission reserve(Track track,std::uint32_t index,
                                    std::uint64_t from_ms,std::uint64_t to_ms,
                                    std::uint64_t content_length) noexcept {
        if(state_==State::Idle || state_==State::Failed ||
           state_==State::Cancelled)return Admission::Inactive;
        if(pending_)return Admission::Busy;
        // The metadata ledger itself is bounded. Refuse network I/O before
        // reaching its fixed 192-piece limit, even for tiny TS segments.
        // Otherwise the transfer would reach committed() and fail fatally
        // after writing a valid file despite plenty of SD byte capacity.
        if(budget_.pieces()>=RollingMediaBudget::kMaxPieces) {
            state_=State::Backpressure;
            return Admission::Backpressure;
        }
        if(from_ms<epoch_ms_ || to_ms<=from_ms ||
           to_ms-from_ms>60000 || index>=4096 ||
           !content_length || content_length>kMaxSegmentBytes ||
           (from_ms>position_ms_ && from_ms-position_ms_>kMaxAheadMs))
            return Admission::Invalid;
        if(content_length > RollingMediaBudget::kCapacityBytes -
                              budget_.used_bytes()) {
            state_=State::Backpressure;
            return Admission::Backpressure;
        }
        pending_=Ticket{++serial_,track,index,from_ms,to_ms,content_length};
        if(state_==State::Backpressure)state_=budget_.startup_ready()
            ? State::Ready : State::Priming;
        return Admission::Accepted;
    }

    [[nodiscard]] std::optional<Ticket> pending_ticket() const noexcept {
        return pending_;
    }

    // Called only after the native transport verified the media and the
    // cache sink fsync/closed its exact-sized file. A mismatched/late ticket
    // cannot publish data belonging to a subsequent seek or retry.
    [[nodiscard]] bool committed(const Ticket& ticket,
                                 std::uint64_t actual_bytes,
                                 bool codec_accepted,bool durable) noexcept {
        if(!pending_ || pending_->request_id!=ticket.request_id ||
           pending_->track!=ticket.track ||
           pending_->index!=ticket.index ||
           pending_->start_ms!=ticket.start_ms ||
           pending_->end_ms!=ticket.end_ms ||
           pending_->reserved_bytes!=ticket.reserved_bytes) return false;
        pending_.reset();
        if(!codec_accepted || !durable || !actual_bytes ||
           actual_bytes>ticket.reserved_bytes ||
           budget_.add(ticket.track,ticket.index,ticket.start_ms,
                       ticket.end_ms,actual_bytes)!=
                            RollingMediaBudget::AddResult::Accepted) {
            state_=State::Failed;
            return false;
        }
        state_=budget_.startup_ready()?State::Ready:State::Priming;
        return true;
    }

    void cancel_pending() noexcept {
        pending_.reset();
        ++serial_;
        if(state_==State::Backpressure || state_==State::Priming ||
           state_==State::Ready)
            state_=budget_.startup_ready()?State::Ready:State::Priming;
    }
    void mark_codecs_verified(bool h264,bool aac) noexcept {
        budget_.set_codec_verification(h264,aac);
        if(state_==State::Priming || state_==State::Ready)
            state_=budget_.startup_ready()?State::Ready:State::Priming;
    }
    [[nodiscard]] bool advance_playhead(std::uint64_t position_ms) noexcept {
        if(position_ms<position_ms_ || position_ms<epoch_ms_ ||
           state_==State::Failed || state_==State::Cancelled ||
           state_==State::Idle)return false;
        position_ms_=position_ms;
        return true;
    }
    [[nodiscard]] bool pin(Track track,std::uint32_t index,bool pinned) noexcept {
        return budget_.pin(track,index,pinned);
    }
    // The file must ALREADY have been safely unlinked, with any decoder
    // descriptor released. Never pre-credit storage before actual deletion.
    [[nodiscard]] bool acknowledge_unlinked(Track track,std::uint32_t index) noexcept {
        const bool erased=budget_.commit_eviction(track,index,position_ms_);
        if(erased && state_==State::Backpressure)
            state_=budget_.startup_ready()?State::Ready:State::Priming;
        return erased;
    }
    [[nodiscard]] bool seek(std::uint64_t position_ms,
                            bool previous_files_purged) noexcept {
        if(!previous_files_purged || pending_ ||
           !budget_.begin_epoch(position_ms))return false;
        epoch_ms_=position_ms_=position_ms;
        ++serial_;
        state_=State::Priming;
        return true;
    }
    [[nodiscard]] bool cancel(bool previous_files_purged) noexcept {
        // A worker must join and the stream/cache must be closed externally.
        // Refuse to forget any accounted media while SD bytes remain.
        if(!previous_files_purged || pending_ || !budget_.clear_epoch())
            return false;
        ++serial_;
        state_=State::Cancelled;
        return true;
    }

    [[nodiscard]] State state() const noexcept {return state_;}
    [[nodiscard]] std::uint64_t media_bytes() const noexcept {
        return budget_.used_bytes();
    }
    [[nodiscard]] std::uint64_t peak_media_bytes() const noexcept {
        return budget_.high_water_bytes();
    }
    [[nodiscard]] std::uint64_t reserved_bytes() const noexcept {
        return budget_.used_bytes()+(pending_?pending_->reserved_bytes:0);
    }
    [[nodiscard]] std::uint64_t evictable_bytes() const noexcept {
        return budget_.evictable_bytes(position_ms_);
    }
    [[nodiscard]] unsigned selected_height() const noexcept {return height_;}
    [[nodiscard]] unsigned native_fps() const noexcept {return fps_;}
    [[nodiscard]] bool startup_ready() const noexcept {
        return state_==State::Ready;
    }
private:
    RollingMediaBudget budget_{};
    std::optional<Ticket> pending_{};
    std::uint64_t serial_{0};
    std::uint64_t epoch_ms_{0},position_ms_{0};
    unsigned height_{0},fps_{0};
    State state_{State::Idle};
};
} // namespace ttnx::playback
