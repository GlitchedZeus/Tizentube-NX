#include "tizentube_nx/core/guest_browse.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <unordered_set>
#include <utility>

namespace ttnx::core {
namespace {

constexpr std::size_t kMaxResultIdBytes = 128;
constexpr std::size_t kMaxTitleBytes = 512;
constexpr std::size_t kMaxChannelTextBytes = 256;
constexpr std::size_t kMaxMetadataTextBytes = 256;
constexpr std::size_t kMaxAccessibilityBytes = 1024;
constexpr std::size_t kMaxThumbnailUrlBytes = 2048;
constexpr std::size_t kMaxThumbnailInputCandidates = 32;
constexpr std::size_t kMaxThumbnailCandidates = 8;

std::string lowercase(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool contains_any(std::string_view value, std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
        if (value.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

std::string bounded_text(const std::string& value, std::size_t max_bytes) {
    return value.size() <= max_bytes ? value : std::string{};
}

bool safe_https_reference(std::string_view value) {
    if (value.empty() || value.size() > kMaxThumbnailUrlBytes || !value.starts_with("https://")) {
        return false;
    }
    for (const unsigned char c : value) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    return true;
}

bool valid_duration_text(std::string_view value) {
    if (value.empty()) return false;
    if (value.size() > 32 || value.front() == ':' || value.back() == ':') return false;

    bool previous_colon = false;
    bool saw_digit = false;
    for (const unsigned char c : value) {
        if (c == ':') {
            if (previous_colon) return false;
            previous_colon = true;
            continue;
        }
        if (!std::isdigit(c)) return false;
        previous_colon = false;
        saw_digit = true;
    }
    return saw_digit;
}

std::vector<ThumbnailCandidate> normalize_thumbnails(const RendererRecord& record) {
    std::vector<ThumbnailCandidate> candidates;
    candidates.reserve(std::min<std::size_t>(
        record.thumbnails.size() + (record.thumbnail_url.empty() ? 0U : 1U),
        kMaxThumbnailInputCandidates));

    std::unordered_set<std::string> seen;
    seen.reserve(kMaxThumbnailInputCandidates);

    auto add = [&](const ThumbnailCandidate& candidate) {
        if (candidates.size() >= kMaxThumbnailInputCandidates) return;
        if (!safe_https_reference(candidate.url)) return;
        if (!seen.insert(candidate.url).second) return;
        candidates.push_back(candidate);
    };

    for (const auto& candidate : record.thumbnails) add(candidate);
    if (!record.thumbnail_url.empty()) {
        add(ThumbnailCandidate{record.thumbnail_url, 0, 0});
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        const std::uint64_t left_area =
            static_cast<std::uint64_t>(left.width) * static_cast<std::uint64_t>(left.height);
        const std::uint64_t right_area =
            static_cast<std::uint64_t>(right.width) * static_cast<std::uint64_t>(right.height);
        if (left_area != right_area) return left_area > right_area;
        if (left.width != right.width) return left.width > right.width;
        if (left.height != right.height) return left.height > right.height;
        return left.url < right.url;
    });
    if (candidates.size() > kMaxThumbnailCandidates) {
        candidates.resize(kMaxThumbnailCandidates);
    }
    return candidates;
}

std::optional<BrowseResult> normalize_renderer_record(const RendererRecord& record) {
    BrowseResult result;
    switch (record.item.kind) {
        case ContentKind::Video:
            result.kind = BrowseResultKind::Video;
            break;
        case ContentKind::Live:
            result.kind = BrowseResultKind::Video;
            result.live = true;
            break;
        case ContentKind::Channel:
            result.kind = BrowseResultKind::Channel;
            break;
        case ContentKind::Playlist:
            result.kind = BrowseResultKind::Playlist;
            break;
        case ContentKind::Short:
        case ContentKind::Unknown:
            return std::nullopt;
    }

    if (record.item.id.empty() || record.item.id.size() > kMaxResultIdBytes ||
        record.item.title.empty() || record.item.title.size() > kMaxTitleBytes) {
        return std::nullopt;
    }

    result.id = record.item.id;
    result.title = record.item.title;
    result.channel_name = bounded_text(record.channel_title, kMaxChannelTextBytes);
    result.channel_id = bounded_text(record.channel_id, kMaxResultIdBytes);

    result.thumbnails = normalize_thumbnails(record);
    if (!result.thumbnails.empty()) result.thumbnail_url = result.thumbnails.front().url;

    result.duration_text = valid_duration_text(record.duration_text)
        ? record.duration_text
        : std::string{};
    result.duration_seconds = result.duration_text.empty()
        ? std::nullopt
        : record.duration_seconds;

    result.view_count_text = bounded_text(record.view_count_text, kMaxMetadataTextBytes);
    result.view_count = record.view_count;
    result.published_text = bounded_text(record.published_text, kMaxMetadataTextBytes);
    result.subscriber_count_text = bounded_text(
        record.subscriber_count_text, kMaxMetadataTextBytes);
    result.subscriber_count = record.subscriber_count;
    result.video_count_text = bounded_text(record.video_count_text, kMaxMetadataTextBytes);
    result.video_count = record.video_count;
    result.accessibility_text = bounded_text(record.accessibility_text, kMaxAccessibilityBytes);

    result.upcoming = record.upcoming;
    result.scheduled_start_time_seconds = record.upcoming
        ? record.scheduled_start_time_seconds
        : std::nullopt;
    return result;
}

}  // namespace

bool valid_guest_request(const GuestRequest& request) {
    if (request.is_continuation()) return true;

    switch (request.surface) {
        case BrowseSurface::Home:
            return request.value.empty();
        case BrowseSurface::Search:
        case BrowseSurface::Channel:
        case BrowseSurface::Playlist:
            return !request.value.empty();
    }
    return false;
}

ContentKind classify_renderer(std::string_view renderer_name) {
    const auto name = lowercase(renderer_name);

    // YouTube has used both "reel" and "shorts" names for Shorts surfaces.
    if (contains_any(name, {"short", "reel"})) return ContentKind::Short;
    if (contains_any(name, {"channelrenderer"})) return ContentKind::Channel;
    if (contains_any(name, {"playlistrenderer", "radiorenderer"})) return ContentKind::Playlist;
    if (contains_any(name, {"videorenderer"})) return ContentKind::Video;
    return ContentKind::Unknown;
}

RendererDisposition renderer_disposition(const RendererRecord& record, const FilterPolicy& policy) {
    const auto name = lowercase(record.renderer);

    // Renderer-name checks happen before type classification so an ad or Shorts
    // renderer cannot disguise itself by carrying a generic Video content kind.
    if (contains_any(name, {"short", "reel"})) return RendererDisposition::DropShorts;
    if (contains_any(name, {"promoted", "adslot", "displayad", "instreamad", "mastheadad"})) {
        return RendererDisposition::DropPromoted;
    }
    if (contains_any(name, {"shopping", "product", "merchandise"})) {
        return RendererDisposition::DropShopping;
    }

    if (record.item.kind == ContentKind::Short) return RendererDisposition::DropShorts;
    if (record.item.promoted) return RendererDisposition::DropPromoted;
    if (record.item.shopping && policy.hide_shopping) return RendererDisposition::DropShopping;

    const auto renderer_kind = classify_renderer(record.renderer);
    const auto effective_kind = record.item.kind == ContentKind::Unknown
        ? renderer_kind
        : record.item.kind;

    if (effective_kind == ContentKind::Unknown) return RendererDisposition::DropUnsupported;
    if (should_hide(record.item, policy)) {
        if (record.item.promoted) return RendererDisposition::DropPromoted;
        if (record.item.shopping) return RendererDisposition::DropShopping;
        return RendererDisposition::DropShorts;
    }

    return RendererDisposition::Allow;
}

BrowsePage sanitize_guest_page(
    std::vector<RendererRecord> records,
    std::string continuation,
    const FilterPolicy& policy) {
    BrowsePage page;
    page.items.reserve(records.size());
    page.results.reserve(records.size());
    page.continuation = std::move(continuation);

    std::unordered_set<std::string> seen;
    seen.reserve(records.size());

    for (auto& record : records) {
        if (renderer_disposition(record, policy) != RendererDisposition::Allow) continue;
        if (record.item.kind == ContentKind::Unknown) {
            record.item.kind = classify_renderer(record.renderer);
        }

        auto normalized = normalize_renderer_record(record);
        if (!normalized) continue;

        const auto identity = browse_result_identity(*normalized);
        if (identity.empty() || !seen.insert(identity).second) continue;

        page.results.push_back(std::move(*normalized));
        page.items.push_back(std::move(record));
    }

    return page;
}

}  // namespace ttnx::core
