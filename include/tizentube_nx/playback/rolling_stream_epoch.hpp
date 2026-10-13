// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tizentube_nx/playback/verified_ring_hls_ingest.hpp"
#include "tizentube_nx/playback/rolling_mpv_stream_bridge.hpp"
#include "tizentube_nx/playback/rolling_prefetch_window.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ttnx::playback {
// One worker, one SD ring, one optional libmpv decoder epoch.
// The native caller must destroy its mpv renderer and mpv_handle before
// notify_decoder_destroyed(). Never seek/evict based on a guessed playhead.
class RollingStreamEpoch final {
public:
    using AllowUrl=std::function<bool(std::string_view)>;
    using Fetch=std::function<VerifiedRingHlsIngest::Result(
        std::shared_ptr<RollingFileStream>,std::string_view)>;
    enum class Phase : std::uint8_t {
        Prepared, Fetching, PrefixReady, WaitingForPlayback, Backpressure, Complete,
        Failed, StopRequested, Joined, DecoderClosed, Cleaned
    };
    struct Snapshot {
        Phase phase{Phase::Prepared};
        std::size_t segments_committed{0};
        std::size_t total_segments{0};
        // Absolute segment boundaries for a verified decoder-close re-prime.
        std::size_t next_segment_index{0};
        std::size_t epoch_origin_segment{0};
        std::uint64_t published_bytes{0};
        std::uint64_t peak_file_bytes{0};
        bool decoder_attached{false};
        bool producer_joined{false};
        bool waiting_for_playback{false};
    };
    static std::unique_ptr<RollingStreamEpoch> create(
        std::string app_owned_path,std::vector<std::string> approved_segment_urls,
        Fetch fetch,AllowUrl allow,
        std::uint64_t capacity=RollingFileStream::kMaximumMedia,
        std::vector<std::uint64_t> segment_durations_ms={}) {
        if(app_owned_path.empty() || !fetch || !allow || approved_segment_urls.empty() ||
           approved_segment_urls.size()>4096)return {};
        std::size_t plan_bytes=0;
        for(const auto& url:approved_segment_urls) {
            if(url.empty() || url.size()>4096 ||
               plan_bytes>512*1024-url.size() || !allow(url))return {};
            plan_bytes+=url.size();
        }
        RollingPrefetchWindow window;
        if(!segment_durations_ms.empty() &&
           (segment_durations_ms.size()!=approved_segment_urls.size() ||
            !window.configure(segment_durations_ms)))return {};
        auto ring=RollingFileStream::create(app_owned_path,capacity);
        if(!ring)return {};
        return std::unique_ptr<RollingStreamEpoch>(new RollingStreamEpoch(
            std::move(app_owned_path),std::move(approved_segment_urls),
            std::move(fetch),std::move(ring),capacity,std::move(window)));
    }
    template<class Reader>
    static std::unique_ptr<RollingStreamEpoch> with_reader(
        std::string app_owned_path,std::vector<std::string> urls,
        std::shared_ptr<Reader> reader,AllowUrl allow,
        std::uint64_t capacity=RollingFileStream::kMaximumMedia,
        std::vector<std::uint64_t> segment_durations_ms={}) {
        if(!reader)return {};
        Fetch callback=[reader=std::move(reader)](
            std::shared_ptr<RollingFileStream> ring,std::string_view url) {
            VerifiedRingHlsIngest staging(std::move(ring));
            return staging.ingest(*reader,url);
        };
        return create(std::move(app_owned_path),std::move(urls),
                      std::move(callback),std::move(allow),capacity,
                      std::move(segment_durations_ms));
    }
    RollingStreamEpoch(const RollingStreamEpoch&)=delete;
    RollingStreamEpoch& operator=(const RollingStreamEpoch&)=delete;
    ~RollingStreamEpoch() {
        stop_and_join();
        // The ring remains alive through every decoder cookie, if the caller
        // violates the required mpv teardown order. Never delete a live FD.
        bridge_.reset();
        ring_.reset();
    }
    [[nodiscard]] bool start() {
        std::lock_guard<std::mutex> lock(mu_);
        if(phase_!=Phase::Prepared || !ring_ || worker_.joinable())return false;
        stop_=false;playback_ms_=0;phase_=Phase::Fetching;
        try {worker_=std::thread([this]{run();});}
        catch(...) {phase_=Phase::Failed;return false;}
        return true;
    }
    [[nodiscard]] Snapshot snapshot()const {
        std::lock_guard<std::mutex> lock(mu_);
        const auto stats=ring_?ring_->stats():RollingFileStream::Stats{};
        return {phase_,committed_,urls_.size(),next_,epoch_origin_,
                stats.written_end,stats.physical_size,decoder_attached_,joined_,
                phase_==Phase::WaitingForPlayback};
    }
    [[nodiscard]] std::shared_ptr<RollingFileStream> ring_for_decoder()const {
        std::lock_guard<std::mutex> lock(mu_);
        return (committed_ && ring_ && !stop_ &&
                phase_!=Phase::Failed && !ring_->stats().failed)
            ?ring_:nullptr;
    }
    // Feed only verified decoder time-pos, never speculative UI seeks.
    // An epoch-relative timeline resets on decoder-close/reprime.
    void advance_playback_ms(std::uint64_t ms) noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        if(stop_ || !decoder_attached_ || !window_.configured() ||
           ms<playback_ms_ || ms>window_.total_duration_ms())return;
        playback_ms_=ms;
        wake_.notify_all();
    }
    // Call after mpv_create and BEFORE mpv_initialize. The returned cookie
    // bridge is owned until explicit decoder-destroyed acknowledgement.
    [[nodiscard]] bool register_decoder(mpv_handle* player) {
        std::lock_guard<std::mutex> lock(mu_);
        if(!player || !ring_ || !committed_ || decoder_attached_ || stop_ ||
           phase_==Phase::Failed || ring_->stats().failed)return false;
        auto bridge=std::make_unique<RollingMpvStreamBridge>(ring_);
        if(bridge->register_before_initialize(player)<0)return false;
        bridge_=std::move(bridge);
        decoder_attached_=true;
        return true;
    }
    void request_stop() {
        std::shared_ptr<RollingFileStream> active;
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_=true;
            if(phase_!=Phase::Cleaned && phase_!=Phase::DecoderClosed)
                phase_=Phase::StopRequested;
            active=ring_;
        }
        wake_.notify_all(); // unblock prefetch wait immediately
        if(active)active->cancel(); // unblock decoder read and staging
    }
    void stop_and_join() {
        request_stop();
        if(worker_.joinable())worker_.join();
        std::lock_guard<std::mutex> lock(mu_);
        joined_=true;
        if(phase_==Phase::StopRequested)phase_=Phase::Joined;
    }
    // Must run ONLY after native mpv_render_context_free and
    // mpv_terminate_destroy. The reader count verifies all callbacks closed.
    [[nodiscard]] bool notify_decoder_destroyed() {
        std::lock_guard<std::mutex> lock(mu_);
        if(!joined_ || !decoder_attached_ || !ring_ ||
           ring_->stats().readers!=0)return false;
        bridge_.reset();
        decoder_attached_=false;
        phase_=Phase::DecoderClosed;
        return true;
    }
    [[nodiscard]] bool cleanup() {
        std::lock_guard<std::mutex> lock(mu_);
        if(!joined_ || decoder_attached_ || !ring_ ||
           ring_->stats().readers ||
           (phase_!=Phase::DecoderClosed && phase_!=Phase::Joined &&
            phase_!=Phase::Failed && phase_!=Phase::Complete &&
            phase_!=Phase::Backpressure))return false;
        bridge_.reset();
        ring_.reset(); // final owner unlinks app-owned media file
        phase_=Phase::Cleaned;
        return true;
    }
    // Seek or a full ring requires a NEW epoch after complete decoder close.
    // The same app-owned path is created only after prior media was unlinked.
    [[nodiscard]] bool restart_at(std::size_t segment_index) {
        std::lock_guard<std::mutex> lock(mu_);
        if(phase_!=Phase::Cleaned || !joined_ || bridge_ || ring_ ||
           segment_index>=urls_.size())return false;
        auto fresh=RollingFileStream::create(path_,capacity_);
        if(!fresh)return false;
        ring_=std::move(fresh);
        committed_=0;
        next_=segment_index;
        epoch_origin_=segment_index;
        playback_ms_=0;
        joined_=false;
        stop_=false;
        phase_=Phase::Prepared;
        return true;
    }
private:
    RollingStreamEpoch(std::string path,std::vector<std::string> urls,
        Fetch fetch,std::shared_ptr<RollingFileStream> ring,std::uint64_t capacity,
        RollingPrefetchWindow window)
        : path_(std::move(path)),urls_(std::move(urls)),fetch_(std::move(fetch)),
          ring_(std::move(ring)),capacity_(capacity),window_(std::move(window)){}
    void run() noexcept {
        for(;;) {
            std::size_t current=0;
            {
                std::unique_lock<std::mutex> lock(mu_);
                if(stop_)return;
                if(next_>=urls_.size()) {
                    ring_->finish(true);
                    phase_=Phase::Complete;
                    return;
                }
                // No CDN request beyond a 15-second lead over verified
                // decoder time-pos. A stopped/offscreen player cannot
                // silently download 25 MiB while the user does nothing.
                if(!window_.can_fetch(next_,epoch_origin_,playback_ms_)) {
                    phase_=Phase::WaitingForPlayback;
                    wake_.wait(lock,[this] {
                        return stop_ || next_>=urls_.size() ||
                            window_.can_fetch(next_,epoch_origin_,playback_ms_);
                    });
                    if(stop_)return;
                }
                phase_=Phase::Fetching;
                current=next_;
            }
            VerifiedRingHlsIngest::Result result=VerifiedRingHlsIngest::Result::Transport;
            try {result=fetch_(ring_,urls_[current]);}catch(...){}
            std::lock_guard<std::mutex> lock(mu_);
            if(stop_)return;
            if(result==VerifiedRingHlsIngest::Result::Backpressure) {
                // A full, independently verified next TS segment will not
                // fit while mpv holds the previous prefix. Seal that prefix
                // with clean EOF; otherwise mpv can block at written_end
                // forever and never release its reader for safe re-prime.
                // No live decoder bytes are overwritten or evicted.
                ring_->finish(true);
                phase_=Phase::Backpressure;
                return;
            }
            if(result!=VerifiedRingHlsIngest::Result::Committed) {
                phase_=Phase::Failed;
                ring_->finish(false);
                return;
            }
            ++next_;++committed_;
            phase_=Phase::PrefixReady;
        }
    }
    const std::string path_;
    std::vector<std::string> urls_;
    Fetch fetch_;
    std::shared_ptr<RollingFileStream> ring_;
    const std::uint64_t capacity_;
    std::unique_ptr<RollingMpvStreamBridge> bridge_;
    mutable std::mutex mu_;
    std::condition_variable wake_{};
    std::thread worker_;
    std::size_t next_{0},committed_{0},epoch_origin_{0};
    std::uint64_t playback_ms_{0};
    RollingPrefetchWindow window_{};
    Phase phase_{Phase::Prepared};
    bool stop_{false},joined_{false},decoder_attached_{false};
};
} // namespace ttnx::playback
