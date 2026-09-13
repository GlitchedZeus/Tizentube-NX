#pragma once

#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/search_result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::core {

enum class BrowseSurface {
    Home,
    Search,
    Channel,
    Playlist,
};

struct GuestRequest {
    BrowseSurface surface{BrowseSurface::Home};
    // Search text for Search; browse/channel/playlist id for the other keyed
    // surfaces. Home intentionally leaves this empty.
    std::string value;
    // Opaque token returned by the previous page. TizenTube NX never inspects
    // or rewrites continuation tokens.
    std::string continuation;

    [[nodiscard]] bool is_continuation() const noexcept {
        return !continuation.empty();
    }
};

// Parser/firewall record. It contains no raw JSON, but it still carries the
// renderer name needed to enforce the no-Shorts/no-ad trust boundary. UI code
// should consume BrowsePage::results instead.
struct RendererRecord {
    std::string renderer;
    ContentItem item;
    std::string channel_title;
    std::string channel_id;

    std::vector<ThumbnailCandidate> thumbnails;
    // Legacy single-thumbnail slot retained while existing parser tests migrate.
    std::string thumbnail_url;

    std::string duration_text;
    std::optional<std::uint64_t> duration_seconds;
    std::string view_count_text;
    std::optional<std::uint64_t> view_count;
    std::string published_text;
    std::string subscriber_count_text;
    std::optional<std::uint64_t> subscriber_count;
    std::string video_count_text;
    std::optional<std::uint64_t> video_count;
    std::string accessibility_text;

    bool upcoming{false};
    std::optional<std::uint64_t> scheduled_start_time_seconds;
};

struct BrowsePage {
    // Internal sanitized records retained while the parser test suite exercises
    // renderer-firewall behavior. Future UI/controller code must use results.
    std::vector<RendererRecord> items;
    // Renderer-agnostic normalized data intended for UI/controller consumers.
    std::vector<BrowseResult> results;
    std::string continuation;

    [[nodiscard]] bool has_more() const noexcept {
        return !continuation.empty();
    }
};

enum class RendererDisposition {
    Allow,
    DropShorts,
    DropPromoted,
    DropShopping,
    DropUnsupported,
};

[[nodiscard]] bool valid_guest_request(const GuestRequest& request);
[[nodiscard]] ContentKind classify_renderer(std::string_view renderer_name);
[[nodiscard]] RendererDisposition renderer_disposition(
    const RendererRecord& record,
    const FilterPolicy& policy = {});
[[nodiscard]] BrowsePage sanitize_guest_page(
    std::vector<RendererRecord> records,
    std::string continuation,
    const FilterPolicy& policy = {});

}  // namespace ttnx::core
