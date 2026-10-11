#pragma once
#include "tizentube_nx/playback/hls_probe.hpp"
#include "tizentube_nx/playback/hls_segment.hpp"
#include "tizentube_nx/playback/media_chunks.hpp"

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
    // Separate AAC MP4 is appended only under the same pending clip
    // transaction; default refuses audio until a reviewed sink supports it.
    virtual bool append_external_aac(std::string_view) { return false; }
    virtual bool publish(const HlsProbePlan& plan)=0;
    virtual void abort() noexcept=0;
};

enum class HlsCacheState { Idle, Fetching, Ready, Failed, Cancelled };
enum class HlsCacheFailure {
    None, InvalidPlaylist, UnableToStage, MediaFetch, MediaWrite,
    TotalSizeExceeded, UnableToPublish, MissingAudio
};

// Fetches ONE independently verified HTTPS HLS segment per call. The caller
// must run pump() only on a background worker. No mpv activity is authorized
// until state() == Ready and the sink has atomically published both tracks.
// Long YouTube videos should eventually use progressive playback/buffering
// rather than waiting for every segment to download.
class HlsCacheJob {
public:
    HlsCacheJob(HlsSegmentReader& reader,HlsCacheSink& sink,
                MediaChunkReader* external_aac_reader=nullptr)
        : reader_(reader),sink_(sink),aac_reader_(external_aac_reader) {}
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
    MediaChunkReader* aac_reader_{nullptr};
    std::optional<HlsProbePlan> active_;
    HlsCacheState state_{HlsCacheState::Idle};
    HlsCacheFailure error_{HlsCacheFailure::None};
    std::uint64_t bytes_{0};
    std::size_t completed_{0};
    std::size_t next_segment_{0};
    unsigned track_{0}; // 0 HLS video, 1 HLS audio, 2 ranged AAC MP4
    bool init_pending_{false};
    bool muxed_audio_verified_{false};
    std::uint64_t aac_offset_{0}, aac_total_{0};
    static constexpr std::uint64_t kMaxAacBytes=128ULL*1024*1024;
};

} // namespace ttnx::playback
