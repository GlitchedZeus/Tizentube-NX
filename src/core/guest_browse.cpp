#include "tizentube_nx/core/guest_browse.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <unordered_set>
#include <utility>

namespace ttnx::core {
namespace {

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

    if (record.item.id.empty() || record.item.title.empty()) return std::nullopt;

    result.id = record.item.id;
    result.title = record.item.title;
    result.channel_name = record.channel_title;
    result.channel_id = record.channel_id;
    result.thumbnail_url = record.thumbnail_url;
    result.duration_text = record.duration_text;
    result.view_count_text = record.view_count_text;
    result.published_text = record.published_text;
    result.subscriber_count_text = record.subscriber_count_text;
    result.video_count_text = record.video_count_text;
    result.accessibility_text = record.accessibility_text;
    result.upcoming = record.upcoming;
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
