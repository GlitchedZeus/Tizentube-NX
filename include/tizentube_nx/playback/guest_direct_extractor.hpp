#pragma once

#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <optional>
#include <string_view>

namespace ttnx::playback {

// Direct, no-cookie guest WEB /player response reader for already-discovered
// *normal* video IDs. Only streamingData formats with clear HTTPS signed URLs
// and supported H.264/AAC are normalized. Ciphered URLs, DRM, unknown formats,
// Shorts, any failed playability, and malformed/big responses fail closed.
// This is NOT a general-purpose Youtube player, JS-decipher implementation,
// browser embed, advertisement playback path, or host network authorization.
[[nodiscard]] std::optional<ExtractionResult> parse_direct_guest_player(
    std::string_view body, std::string_view expected_video_id);

enum class DirectPlayerFailure {
    None,
    InvalidRequest,
    HttpTransport,
    HttpStatus,
    MalformedResponse,
    PlayabilityBlocked,
    WrongIdentity,
    ShortsOrLive,
    MissingStreamingData,
    NoSupportedDirectFormats
};

// Stable safe diagnostic codes only. Never include raw player response, stream
// URL, cookies, auth fields or server-supplied arbitrary error descriptions.
[[nodiscard]] const char* describe_direct_player_failure(DirectPlayerFailure failure);

// Reuses the real, already-accepted guest www.youtube.com HTTPS client.
// This does not fetch Googlevideo media (that needs the distinct M3 policy).
class GuestDirectExtractor final : public Extractor {
public:
    GuestDirectExtractor(net::HttpClient& browse_http,
                         const youtube::GuestSession& session)
        : browse_http_(browse_http), session_(session) {}
    [[nodiscard]] std::optional<ExtractionResult> resolve(
        std::string_view normal_video_id) override;
    [[nodiscard]] DirectPlayerFailure last_failure() const noexcept { return last_failure_; }
    [[nodiscard]] int last_http_status() const noexcept { return last_http_status_; }
private:
    net::HttpClient& browse_http_;
    const youtube::GuestSession& session_;
    DirectPlayerFailure last_failure_{DirectPlayerFailure::None};
    int last_http_status_{0};
};

} // namespace ttnx::playback
