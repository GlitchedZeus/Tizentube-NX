#pragma once

#include "tizentube_nx/net/http.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ttnx::playback {

// M3 media-plane *policy*, not an implicit extension of M2's HTTP allowlist.
// The policy is checked at this boundary and again by any future socket/DNS
// implementation. Do not inject the M2 browse client: it denies Googlevideo.
struct MediaRange {
    std::uint64_t first{0};
    std::uint64_t last{0}; // inclusive
};

enum class MediaReadFailure {
    None,
    InvalidDestination,
    InvalidRange,
    TransportUnavailable,
    TransportError,
    RedirectRejected,
    UnexpectedStatus,
    MissingOrInvalidRange,
    InvalidPayloadHeaders,
    SizeMismatch,
    MediaPayloadRejected,
};

struct MediaReadResult {
    MediaReadFailure failure{MediaReadFailure::TransportUnavailable};
    std::string bytes; // never write signed media URLs or auth values to logs
    std::uint64_t resource_size{0};
    [[nodiscard]] bool ok() const noexcept { return failure == MediaReadFailure::None; }
};

// Only a dedicated media client, reviewed to repeat the media host policy
// immediately before DNS, may implement this. libmpv's own URL fetcher is NOT
// a valid MediaChunkTransport because its redirects bypass this check.
class MediaChunkTransport {
public:
    virtual ~MediaChunkTransport() = default;
    virtual ttnx::net::HttpResult perform(const ttnx::net::HttpRequest& request) = 0;
};

// Fail-closed byte-range frontend suitable for bounded buffering. Never
// upgrades M2 networking and never downloads an entire unbounded movie.
// On failure the response is not retained or handed to FFmpeg.
class MediaChunkReader {
public:
    static constexpr std::size_t kMaxChunkBytes = 1024 * 1024;
    explicit MediaChunkReader(MediaChunkTransport& transport) : transport_(transport) {}
    [[nodiscard]] MediaReadResult read(std::string_view signed_media_url, MediaRange range);
private:
    MediaChunkTransport& transport_;
};

// A small policy-only helper for future download/seek scheduling. A 64-bit
// position never overflows the last byte of a bounded HTTP range.
[[nodiscard]] std::optional<MediaRange> next_media_range(
    std::uint64_t offset, std::uint64_t known_size = 0,
    std::size_t chunk_bytes = MediaChunkReader::kMaxChunkBytes);

} // namespace ttnx::playback
