#pragma once

#include "tizentube_nx/net/http.hpp"
#include <optional>
#include <functional>
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

// Native opt-in streaming interface. Only the dedicated HTTPS media adapter
// may implement this; never expose libmpv's own HTTP/network stack.
using HlsMediaChunk = std::function<bool(std::string_view)>;
// Called only after HTTP 200, accepted MIME and exact Content-Length.
// Must reserve declared bytes before the stream emits its first body byte.
using HlsMediaAdmission = std::function<bool(std::size_t)>;
struct HlsStreamResult {
    bool ok{false};
    std::size_t bytes{0};
};
class HlsSegmentStreamTransport {
public:
    virtual ~HlsSegmentStreamTransport()=default;
    virtual HlsStreamResult stream_segment(std::string_view signed_url,
                                           const HlsMediaAdmission& admit,
                                           const HlsMediaChunk& sink)=0;
};

// Strict memory-bounded single-segment reader. Media bytes can be delivered to
// an app-owned disposable SD staging sink in a future integration. Do not
// publish partial media as playable, follow redirects or trust libmpv's HTTP.
// A segment can be up to 16 MiB; total playlist/cache caps apply separately.
class HlsSegmentReader {
public:
    explicit HlsSegmentReader(net::HttpClient& media_http,
                              HlsSegmentStreamTransport* stream=nullptr)
        : http_(media_http), stream_(stream) {}
    [[nodiscard]] HlsSegmentBytes fetch(std::string_view signed_url) const;
    // Native option sends <=32-KiB chunks straight to a transactionally
    // staged SD .part. Existing host fakes retain a legacy adapter for tests.
    [[nodiscard]] HlsStreamResult stream(std::string_view signed_url,
                                          const HlsMediaAdmission& admit,
                                          const HlsMediaChunk& emit) const;
    static constexpr std::size_t kMaximumSegmentBytes=16*1024*1024;
private:
    net::HttpClient& http_;
    HlsSegmentStreamTransport* stream_{nullptr};
};

} // namespace ttnx::playback
