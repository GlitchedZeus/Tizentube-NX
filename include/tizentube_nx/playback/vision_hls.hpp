#pragma once

#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ttnx::playback {

struct HlsPlayerSource {
    std::string video_id;
    std::string master_playlist_url;
    std::uint64_t duration_ms{0};
};

// Strictly bounded guest video identity, playability, non-live and HLS source.
// No bare streamingData URL is authorization to resolve any arbitrary network
// host. The next stage must check the playlist itself, every segment and DRM.
[[nodiscard]] std::optional<HlsPlayerSource> parse_m3_guest_hls_source(
    std::string_view json, std::string_view expected_normal_video_id);

enum class HlsSourceError {
    None, InvalidRequest, Transport, HttpStatus, ResponseRejected
};

class VisionHlsSource {
public:
    VisionHlsSource(net::HttpClient& http, const youtube::GuestSession& web_guest)
        : http_(http), web_guest_(web_guest) {}
    [[nodiscard]] std::optional<HlsPlayerSource> resolve(std::string_view video_id);
    [[nodiscard]] HlsSourceError last_error() const noexcept { return last_error_; }
    [[nodiscard]] long last_status() const noexcept { return status_; }
private:
    net::HttpClient& http_;
    const youtube::GuestSession& web_guest_;
    HlsSourceError last_error_{HlsSourceError::None};
    long status_{0};
};

} // namespace ttnx::playback
