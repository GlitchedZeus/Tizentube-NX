#include "tizentube_nx/playback/playback.hpp"

#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/net/outbound_policy.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <tuple>

namespace ttnx::playback {
namespace {

bool dns_label_valid(std::string_view label) {
    if (label.empty() || label.size() > 63) return false;
    auto alnum = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    if (!alnum(static_cast<unsigned char>(label.front())) ||
        !alnum(static_cast<unsigned char>(label.back()))) return false;
    for (const unsigned char c : label) {
        if (!alnum(c) && c != '-') return false;
    }
    return true;
}

bool valid_googlevideo_host(std::string_view host) {
    constexpr std::string_view suffix = ".googlevideo.com";
    if (host.size() <= suffix.size() || host.size() > 253 ||
        !host.ends_with(suffix)) return false;
    if (ttnx::net::is_nintendo_host(host)) return false;
    auto remaining = host.substr(0, host.size() - suffix.size());
    while (!remaining.empty()) {
        const auto dot = remaining.find('.');
        const auto part = remaining.substr(0, dot);
        if (!dns_label_valid(part)) return false;
        if (dot == std::string_view::npos) return true;
        remaining.remove_prefix(dot + 1);
    }
    return false;
}

bool supported_video(const VideoFormat& f, const QualityPolicy& p) {
    if (f.drm || f.hdr || f.bit_depth != 8 || f.width == 0 || f.height == 0 ||
        f.width > 4096 || f.height > 2160 || f.fps == 0 ||
        f.fps > p.max_fps || f.fps > 60 ||
        f.height > p.max_height || f.bitrate == 0) return false;
    if (f.codec == VideoCodec::H264) return true;
    return p.permit_vp9 && f.codec == VideoCodec::Vp9;
}

bool supported_audio(const AudioFormat& f) {
    return !f.drm && f.bitrate > 0 && f.bitrate <= 1'000'000 &&
           (f.codec == AudioCodec::Aac || f.codec == AudioCodec::Opus);
}

} // namespace

bool is_reviewable_googlevideo_https_url(std::string_view url) {
    // Fail before DNS: no credentials, alternate ports, IP literals, fragments,
    // control bytes or arbitrary hosts. Never treat this as an M2 allowlist.
    if (url.size() > 8192 || !url.starts_with("https://")) return false;
    for (const unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    const auto authority_start = std::string_view("https://").size();
    const auto end = url.find_first_of("/?#", authority_start);
    const auto authority = url.substr(
        authority_start,
        end == std::string_view::npos ? end : end - authority_start);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find('[') != std::string_view::npos ||
        authority.find(']') != std::string_view::npos ||
        url.find('#') != std::string_view::npos) return false;

    auto host = authority;
    if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
        if (authority.substr(colon) != ":443") return false;
        host = authority.substr(0, colon);
    }
    std::string lowercase;
    lowercase.reserve(host.size());
    for (const unsigned char c : host) {
        if (c > 0x7f) return false;
        lowercase.push_back(static_cast<char>(std::tolower(c)));
    }
    if (!valid_googlevideo_host(lowercase)) return false;
    if (end == std::string_view::npos || url[end] != '/') return false;
    const auto path = url.substr(end);
    return path == "/videoplayback" || path.starts_with("/videoplayback?");
}

SelectionResult select_streams(const ExtractionResult& data, const QualityPolicy& policy) {
    if (!ttnx::core::is_valid_video_id(data.video_id))
        return {{}, SelectionError::InvalidVideoId};
    if (data.is_short) return {{}, SelectionError::ShortsForbidden};
    if (data.duration_ms > 7ULL * 24 * 60 * 60 * 1000)
        return {{}, SelectionError::UnusableDuration};
    if (policy.max_height < 144 || policy.max_height > 2160 ||
        policy.max_fps == 0 || policy.max_fps > 60 ||
        data.video.size() > 512 || data.audio.size() > 512)
        return {{}, SelectionError::NoCompatibleVideo};

    const AudioFormat* best_audio = nullptr;
    bool unsafe_url = false;
    for (const auto& f : data.audio) {
        if (!supported_audio(f)) continue;
        if (!is_reviewable_googlevideo_https_url(f.url)) {
            unsafe_url = true;
            continue;
        }
        if (!best_audio ||
            std::tuple(f.codec == AudioCodec::Aac, f.bitrate) >
            std::tuple(best_audio->codec == AudioCodec::Aac, best_audio->bitrate))
            best_audio = &f;
    }

    const VideoFormat* best_video = nullptr;
    bool best_adaptive = false;
    std::tuple<int, unsigned, unsigned, int, std::uint64_t> best_score{};
    for (const auto& f : data.video) {
        if (!supported_video(f, policy)) continue;
        if (!is_reviewable_googlevideo_https_url(f.url)) {
            unsafe_url = true;
            continue;
        }
        const bool muxed = f.muxed_audio == AudioCodec::Aac ||
                           f.muxed_audio == AudioCodec::Opus;
        const bool adaptive = best_audio != nullptr;
        if (!muxed && !adaptive) continue;
        const bool use_adaptive = adaptive && (policy.prefer_adaptive || !muxed);
        // H.264 is deliberately ranked above VP9 until hardware is proven.
        const auto score = std::tuple(
            f.codec == VideoCodec::H264 ? 2 : 1,
            f.height, f.fps,
            use_adaptive == policy.prefer_adaptive ? 1 : 0,
            f.bitrate);
        if (!best_video || score > best_score) {
            best_video = &f;
            best_adaptive = use_adaptive;
            best_score = score;
        }
    }
    if (!best_video) {
        if (unsafe_url) return {{}, SelectionError::UnsafeMediaUrl};
        if (data.video.empty()) return {{}, SelectionError::NoCompatibleVideo};
        return {{}, best_audio ? SelectionError::NoCompatibleVideo
                               : SelectionError::NoCompatibleAudio};
    }

    SelectedStreams selected;
    selected.video_url = best_video->url;
    selected.video_codec = best_video->codec;
    selected.width = best_video->width;
    selected.height = best_video->height;
    selected.fps = best_video->fps;
    selected.duration_ms = data.duration_ms;
    selected.is_live = data.is_live;
    selected.adaptive = best_adaptive;
    if (best_adaptive) {
        selected.audio_url = best_audio->url;
        selected.audio_codec = best_audio->codec;
    } else {
        selected.audio_codec = best_video->muxed_audio;
    }
    return {std::move(selected), SelectionError::None};
}

bool PlaybackSession::play(const SelectedStreams& streams) {
    // Leave no old playback running if a new source cannot be used.
    stop();
    if (!is_reviewable_googlevideo_https_url(streams.video_url) ||
        (streams.adaptive &&
         (!is_reviewable_googlevideo_https_url(streams.audio_url) ||
          streams.audio_codec == AudioCodec::Unknown)) ||
        (!streams.adaptive && (!streams.audio_url.empty() ||
                              streams.audio_codec == AudioCodec::Unknown))) {
        state_ = PlayerState::Error;
        return false;
    }
    duration_ms_ = streams.duration_ms;
    is_live_ = streams.is_live;
    if (!backend_.load(streams)) {
        backend_.stop();
        state_ = PlayerState::Error;
        return false;
    }
    state_ = PlayerState::Playing;
    return true;
}

bool PlaybackSession::pause() {
    if (state_ != PlayerState::Playing) return false;
    if (!backend_.pause(true)) { state_ = PlayerState::Error; return false; }
    state_ = PlayerState::Paused;
    return true;
}

bool PlaybackSession::resume() {
    if (state_ != PlayerState::Paused) return false;
    if (!backend_.pause(false)) { state_ = PlayerState::Error; return false; }
    state_ = PlayerState::Playing;
    return true;
}

bool PlaybackSession::seek(std::uint64_t position_ms) {
    if (state_ != PlayerState::Playing && state_ != PlayerState::Paused) return false;
    if (is_live_ || (duration_ms_ > 0 && position_ms > duration_ms_)) return false;
    if (!backend_.seek(position_ms)) { state_ = PlayerState::Error; return false; }
    return true;
}

void PlaybackSession::stop() noexcept {
    backend_.stop();
    state_ = PlayerState::Idle;
    duration_ms_ = 0;
    is_live_ = false;
}

LaunchResult PlaybackLauncher::launch(
    std::string_view normal_video_id, const QualityPolicy& policy) {
    if (!ttnx::core::is_valid_video_id(normal_video_id))
        return {LaunchError::InvalidVideoId, SelectionError::InvalidVideoId};
    // Never allow an already-playing old video to survive a failed launch.
    player_.stop();
    const auto resolved = extractor_.resolve(normal_video_id);
    if (!resolved)
        return {LaunchError::ExtractionFailed, SelectionError::None};
    if (resolved->video_id != normal_video_id)
        return {LaunchError::IdentityMismatch, SelectionError::InvalidVideoId};
    const auto selected = select_streams(*resolved, policy);
    if (!selected.streams)
        return {LaunchError::StreamRejected, selected.error};
    if (!player_.play(*selected.streams))
        return {LaunchError::PlayerFailed, SelectionError::None};
    return {};
}

} // namespace ttnx::playback
