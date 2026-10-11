#pragma once
#include "tizentube_nx/playback/hls_probe.hpp"
#include "tizentube_nx/playback/hls_segment.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace ttnx::playback {

// Application-owned temporary media cache. No user path input, no writes to
// installed games, saves or any user's existing source files. The native
// implementation must stage to .part files and publish only after every audio
// AND video file, manifest and duration check succeeds.
class HlsCacheSink {
public:
    virtual ~HlsCacheSink() = default;
    virtual bool begin(const HlsProbePlan& plan)=0;
    virtual bool write_segment(bool audio,bool initialization,
                               std::size_t index,std::string_view bytes)=0;
    virtual bool publish(const HlsProbePlan& plan)=0;
    virtual void abort() noexcept=0;
};

enum class HlsCacheState { Idle, Fetching, Ready, Failed, Cancelled };
enum class HlsCacheFailure {
    None, InvalidPlaylist, UnableToStage, MediaFetch, MediaWrite,
    TotalSizeExceeded, UnableToPublish
};

// Fetches ONE independently verified HTTPS HLS segment per call. The caller
// must run pump() only on a background worker. No mpv activity is authorized
// until state() == Ready and the sink has atomically published both tracks.
// Long YouTube videos should eventually use progressive playback/buffering
// rather than waiting for every segment to download.
class HlsCacheJob {
public:
    HlsCacheJob(HlsSegmentReader& reader,HlsCacheSink& sink)
        : reader_(reader),sink_(sink) {}
    ~HlsCacheJob(){cancel();}
    [[nodiscard]] bool start(const HlsProbePlan& plan);
    [[nodiscard]] HlsCacheState pump();
    void cancel() noexcept;
    [[nodiscard]] HlsCacheState state() const noexcept {return state_;}
    [[nodiscard]] HlsCacheFailure error() const noexcept {return error_;}
    [[nodiscard]] std::uint64_t downloaded_bytes() const noexcept {return bytes_;}
    [[nodiscard]] std::size_t segments_completed() const noexcept {return completed_;}
    static constexpr std::uint64_t kMaxTotalBytes=512ULL*1024*1024;
private:
    HlsSegmentReader& reader_;
    HlsCacheSink& sink_;
    std::optional<HlsProbePlan> active_;
    HlsCacheState state_{HlsCacheState::Idle};
    HlsCacheFailure error_{HlsCacheFailure::None};
    std::uint64_t bytes_{0};
    std::size_t completed_{0};
    std::size_t next_segment_{0};
    unsigned track_{0}; // 0 video, 1 separate audio, 2 complete
    bool init_pending_{false};
};

} // namespace ttnx::playback
