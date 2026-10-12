#pragma once

#include "tizentube_nx/net/http.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace ttnx::playback {

enum class HlsSegmentFailure {
    None, InvalidSource, Transport, HttpStatus, WrongType,
    InvalidLength, CompressedOrAmbiguous, NotMedia,
};

struct HlsSegmentBytes {
    HlsSegmentFailure failure{HlsSegmentFailure::InvalidSource};
    std::string bytes;
    int http_status{0}; // only numeric status, never a bearer URL
    [[nodiscard]] bool ok() const noexcept { return failure==HlsSegmentFailure::None; }
};

// Strict memory-bounded single-segment reader. Media bytes can be delivered to
// an app-owned disposable SD staging sink in a future integration. Do not
// publish partial media as playable, follow redirects or trust libmpv's HTTP.
// A segment can be up to 16 MiB; total playlist/cache caps apply separately.
class HlsSegmentReader {
public:
    explicit HlsSegmentReader(net::HttpClient& media_http) : http_(media_http) {}
    [[nodiscard]] HlsSegmentBytes fetch(std::string_view signed_url) const;
    static constexpr std::size_t kMaximumSegmentBytes=16*1024*1024;
private:
    net::HttpClient& http_;
};

} // namespace ttnx::playback
