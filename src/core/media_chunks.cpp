#include "tizentube_nx/playback/media_chunks.hpp"

#include "tizentube_nx/playback/playback.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace ttnx::playback {
namespace {
constexpr std::uint64_t kMaxResourceSize = 1024ULL * 1024 * 1024 * 1024; // 1 TiB bound

std::string lowercase(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

bool parse_uint(std::string_view s, std::uint64_t& out) {
    if (s.empty() || s.size() > 20 ||
        (s.size() > 1 && s.front() == '0')) return false;
    out = 0;
    for (const unsigned char ch : s) {
        if (ch < '0' || ch > '9') return false;
        const unsigned digit = ch - '0';
        if (out > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return false;
        out = out * 10 + digit;
    }
    return true;
}

struct Header {
    std::string value;
    bool present{false};
    bool duplicate{false};
};
Header get_header(const ttnx::net::HttpResponse& r, std::string_view name) {
    Header out;
    for (const auto& h : r.headers) {
        if (lowercase(h.name) != name) continue;
        if (out.present) out.duplicate = true;
        out.present = true;
        out.value = h.value;
    }
    return out;
}

bool valid_header(const Header& h) {
    if (h.duplicate) return false;
    for (const unsigned char c : h.value) {
        if (c <= 0x1f || c == 0x7f) return false;
    }
    return true;
}

bool read_content_range(std::string_view s, MediaRange requested,
                        std::uint64_t& total, std::uint64_t& delivered) {
    if (!s.starts_with("bytes ")) return false;
    s.remove_prefix(6);
    const auto dash = s.find('-');
    const auto slash = s.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos ||
        dash == 0 || dash >= slash || slash + 1 >= s.size() ||
        s.find('-', dash + 1) != std::string_view::npos ||
        s.find('/', slash + 1) != std::string_view::npos) return false;
    std::uint64_t start = 0, end = 0;
    if (!parse_uint(s.substr(0, dash), start) ||
        !parse_uint(s.substr(dash + 1, slash - dash - 1), end) ||
        !parse_uint(s.substr(slash + 1), total)) return false;
    if (start != requested.first || total == 0 || total > kMaxResourceSize ||
        end < start || end >= total) return false;
    // A byte-range request can legitimately run off EOF. In that case
    // servers return 206 up to exactly resource_size - 1.
    if (end != std::min(requested.last, total - 1)) return false;
    delivered = end - start + 1;
    return delivered <= MediaChunkReader::kMaxChunkBytes;
}

bool media_content_type(std::string_view value) {
    // Reject HTML/error pages and all unknown content before feeding decoders.
    const auto semi = value.find(';');
    const auto media = lowercase(value.substr(0, semi));
    return media == "video/mp4" || media == "audio/mp4" ||
           media == "video/webm" || media == "audio/webm" ||
           media == "application/octet-stream" ||
           media == "binary/octet-stream";
}
} // namespace

std::optional<MediaRange> next_media_range(std::uint64_t offset,
                                          std::uint64_t known_size,
                                          std::size_t chunk_bytes) {
    if (chunk_bytes == 0 || chunk_bytes > MediaChunkReader::kMaxChunkBytes ||
        offset >= kMaxResourceSize ||
        (known_size != 0 && (known_size > kMaxResourceSize ||
                              offset >= known_size))) return std::nullopt;
    auto last = offset + static_cast<std::uint64_t>(chunk_bytes) - 1;
    if (last >= kMaxResourceSize) last = kMaxResourceSize - 1;
    if (known_size != 0) last = std::min(last, known_size - 1);
    return MediaRange{offset, last};
}

MediaReadResult MediaChunkReader::read(std::string_view signed_media_url,
                                       MediaRange range) {
    MediaReadResult out;
    // Check before passing a signed URL into *any* underlying network stack.
    if (!is_reviewable_googlevideo_https_url(signed_media_url)) {
        out.failure = MediaReadFailure::InvalidDestination;
        return out;
    }
    if (range.last < range.first ||
        range.last >= kMaxResourceSize ||
        range.last - range.first >= kMaxChunkBytes) {
        out.failure = MediaReadFailure::InvalidRange;
        return out;
    }

    ttnx::net::HttpRequest request;
    request.method = ttnx::net::HttpMethod::Get;
    request.url = std::string(signed_media_url);
    request.timeout_ms = 15000;
    request.headers = {
        {"Range", "bytes=" + std::to_string(range.first) + "-" +
                  std::to_string(range.last)},
        {"Accept-Encoding", "identity"},
        {"Accept", "application/octet-stream"},
    };
    // The injected media transport MUST separately repeat the DNS/redirect
    // policy at socket-connect time. NEVER inject the M2 browse-only client.
    auto fetched = transport_.perform(request);
    if (!fetched.ok) {
        out.failure = MediaReadFailure::TransportError;
        return out;
    }
    const auto& r = fetched.response;
    if (r.status_code >= 300 && r.status_code < 400) {
        out.failure = MediaReadFailure::RedirectRejected;
        return out;
    }
    if (r.status_code != 206) {
        // Full-body 200 responses are rejected, including if the origin
        // silently ignored the Range header and sent an entire video.
        out.failure = MediaReadFailure::UnexpectedStatus;
        return out;
    }

    const auto location = get_header(r, "location");
    const auto encoding = get_header(r, "content-encoding");
    const auto transfer = get_header(r, "transfer-encoding");
    const auto type = get_header(r, "content-type");
    const auto size = get_header(r, "content-length");
    const auto content_range = get_header(r, "content-range");
    if (!valid_header(location) || !valid_header(encoding) ||
        !valid_header(transfer) || !valid_header(type) ||
        !valid_header(size) || !valid_header(content_range) ||
        location.present || transfer.present ||
        (encoding.present && lowercase(encoding.value) != "identity") ||
        !type.present || !media_content_type(type.value) ||
        !size.present || !content_range.present) {
        out.failure = MediaReadFailure::InvalidPayloadHeaders;
        return out;
    }

    std::uint64_t expected_length = 0;
    std::uint64_t total = 0, delivered = 0;
    if (!read_content_range(content_range.value, range, total, delivered)) {
        out.failure = MediaReadFailure::MissingOrInvalidRange;
        return out;
    }
    if (!parse_uint(size.value, expected_length) || expected_length != delivered) {
        out.failure = MediaReadFailure::SizeMismatch;
        return out;
    }
    if (r.body.size() != expected_length ||
        r.body.size() > kMaxChunkBytes) {
        out.failure = MediaReadFailure::SizeMismatch;
        return out;
    }
    // Deny any HTML response even if mislabeled as a media success.
    if (r.body.starts_with("<!DOCTYPE") || r.body.starts_with("<html") ||
        r.body.starts_with("<?xml")) {
        out.failure = MediaReadFailure::MediaPayloadRejected;
        return out;
    }

    out.failure = MediaReadFailure::None;
    out.resource_size = total;
    out.bytes = std::move(fetched.response.body);
    return out;
}

} // namespace ttnx::playback
