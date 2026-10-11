#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::playback {

// M3 is strictly opt-in. These pure types do not start networking or playback.
// The existing M2 outbound allowlist remains unchanged.
enum class VideoCodec { H264, Vp9, Av1, Unknown };
enum class AudioCodec { Aac, Opus, Unknown };
enum class PlayerState { Idle, Playing, Paused, Error };

struct VideoFormat {
    std::string url;
    VideoCodec codec{VideoCodec::Unknown};
    AudioCodec muxed_audio{AudioCodec::Unknown};
    unsigned width{0};
    unsigned height{0};
    unsigned fps{0};
    unsigned bit_depth{8};
    std::uint64_t bitrate{0};
    bool hdr{false};
    bool drm{false};
};

struct AudioFormat {
    std::string url;
    AudioCodec codec{AudioCodec::Unknown};
    std::uint64_t bitrate{0};
    bool drm{false};
};

struct ExtractionResult {
    std::string video_id;
    std::uint64_t duration_ms{0};
    bool is_live{false};
    bool is_short{false};
    std::vector<VideoFormat> video;
    std::vector<AudioFormat> audio;
};

// A project-owned boundary: the UI knows neither yt-dlp internals nor mpv.
// A future remote/native provider may implement this interface. No login,
// cookie, token, or arbitrary custom-HTTP-header input is accepted.
class Extractor {
public:
    virtual ~Extractor() = default;
    virtual std::optional<ExtractionResult> resolve(std::string_view video_id) = 0;
};

struct QualityPolicy {
    unsigned max_height{1080};
    unsigned max_fps{60};
    bool permit_vp9{false}; // independent Switch hardware acceptance required
    bool prefer_adaptive{true};
};

struct SelectedStreams {
    std::string video_url;
    std::string audio_url; // empty for muxed video+audio
    VideoCodec video_codec{VideoCodec::Unknown};
    AudioCodec audio_codec{AudioCodec::Unknown};
    unsigned width{0};
    unsigned height{0};
    unsigned fps{0};
    std::uint64_t duration_ms{0};
    bool is_live{false};
    bool adaptive{false};
};

enum class SelectionError {
    None,
    InvalidVideoId,
    ShortsForbidden,
    NoCompatibleVideo,
    NoCompatibleAudio,
    UnsafeMediaUrl,
    UnusableDuration,
};

struct SelectionResult {
    std::optional<SelectedStreams> streams;
    SelectionError error{SelectionError::None};
};

// Structural media destination validation only. This is NOT permission for the
// M2 HTTP client to connect. M3 must add an independently reviewed media-plane
// policy at the actual network boundary before enabling any transport.
[[nodiscard]] bool is_reviewable_googlevideo_https_url(std::string_view url);
[[nodiscard]] SelectionResult select_streams(
    const ExtractionResult& extraction, const QualityPolicy& policy = {});

class PlayerBackend {
public:
    virtual ~PlayerBackend() = default;
    virtual bool load(const SelectedStreams& streams) = 0;
    virtual bool pause(bool paused) = 0;
    virtual bool seek(std::uint64_t position_ms) = 0;
    virtual void stop() = 0;
};

// Deterministic control layer. Backend failure is explicit and never falls
// back to a YouTube web player or an ad-bearing playback route.
class PlaybackSession {
public:
    explicit PlaybackSession(PlayerBackend& backend) : backend_(backend) {}

    [[nodiscard]] bool play(const SelectedStreams& streams);
    [[nodiscard]] bool pause();
    [[nodiscard]] bool resume();
    [[nodiscard]] bool seek(std::uint64_t position_ms);
    void stop() noexcept;

    [[nodiscard]] PlayerState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t duration_ms() const noexcept { return duration_ms_; }
    [[nodiscard]] bool live() const noexcept { return is_live_; }
private:
    PlayerBackend& backend_;
    PlayerState state_{PlayerState::Idle};
    std::uint64_t duration_ms_{0};
    bool is_live_{false};
};

} // namespace ttnx::playback
