#pragma once

#include "tizentube_nx/core/content_filter.hpp"

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

struct RendererRecord {
    // Leaf renderer name from the transport/parser boundary. Container
    // renderers should be unwrapped before they reach this model.
    std::string renderer;
    ContentItem item;
    std::string channel_title;
    std::string thumbnail_url;
    std::string duration_text;
};

struct BrowsePage {
    std::vector<RendererRecord> items;
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
