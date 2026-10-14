#pragma once
#include "tizentube_nx/playback/hls_probe.hpp"
#include "tizentube_nx/playback/aac_mp4_structure.hpp"
#include "tizentube_nx/playback/hls_startup_buffer.hpp"
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
    // Optional, non-playable metadata snapshot. It MUST stay in the
    // disposable staging directory; existing ready/ media stays untouched.
    virtual bool stage_startup_event(const HlsProbePlan&,std::size_t) {
        return false;
    }
    virtual bool publish(const HlsProbePlan& plan)=0;
    virtual void abort() noexcept=0;
};

enum class HlsCacheState { Idle, Fetching, Ready, Failed, Cancelled };
enum class HlsCacheFailure {
    None, InvalidPlaylist, UnableToStage, MediaFetch, MediaWrite,
    TotalSizeExceeded, UnableToPublish, MissingAudio, UnsupportedVideo
};
[[nodiscard]] const char* describe_hls_cache_failure(HlsCacheFailure reason) noexcept;

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
    [[nodiscard]] std::size_t total_segments() const noexcept {return total_segments_;}
    [[nodiscard]] bool downloading_external_aac() const noexcept {
        return state_==HlsCacheState::Fetching && track_==2;
    }
    // One network operation per pump; worker sleeps this many milliseconds
    // before retrying a transient error on exactly the same media object.
    [[nodiscard]] unsigned retry_delay_ms() const noexcept {return retry_delay_ms_;}
    [[nodiscard]] unsigned transient_attempts() const noexcept {return transient_attempts_;}
    static constexpr unsigned kMaxTransientRetries=2;
    // Prepared is telemetry, NOT authorization to open pending SD media.
    [[nodiscard]] bool startup_buffer_prepared() const noexcept {
        return startup_buffer_.prepared();
    }
    [[nodiscard]] std::uint64_t startup_buffer_ms() const noexcept {
        return startup_buffer_.prefix_ms();
    }
    [[nodiscard]] bool startup_event_staged() const noexcept {
        return startup_event_staged_;
    }
    [[nodiscard]] std::size_t staged_event_segments() const noexcept {
        return startup_event_segments_;
    }
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
    std::size_t total_segments_{0};
    std::size_t next_segment_{0};
    unsigned track_{0}; // 0 HLS video, 1 HLS audio, 2 ranged AAC MP4
    // Download the separate HLS AAC playlist immediately after one verified
    // video segment. Restore video at the saved position after audio finishes.
    // This avoids waiting for an entire video before discovering audio 403.
    std::size_t paused_video_segment_{0};
    bool early_audio_complete_{false};
    unsigned transient_attempts_{0},retry_delay_ms_{0};
    bool init_pending_{false};
    bool muxed_audio_verified_{false};
    bool video_codec_verified_{false};
    bool audio_codec_verified_{false};
    std::uint64_t aac_offset_{0}, aac_total_{0};
    AacMp4Structure aac_structure_{};
    HlsStartupBuffer startup_buffer_{};
    bool startup_event_staged_{false};
    std::size_t startup_event_segments_{0};
    static constexpr std::uint64_t kMaxAacBytes=128ULL*1024*1024;
};

} // namespace ttnx::playback
