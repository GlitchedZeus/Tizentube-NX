#pragma once

#include <string>

namespace ttnx::core {

enum class ContentKind {
    Video,
    Short,
    Channel,
    Playlist,
    Live,
    Unknown,
};

struct ContentItem {
    ContentKind kind{ContentKind::Unknown};
    std::string id;
    std::string title;
    bool promoted{false};
    bool shopping{false};
};

struct FilterPolicy {
    // Shorts and promoted/ad content are hard project invariants. These fields
    // stay in the policy shape so future settings migrations cannot accidentally
    // reinterpret old data as permission to surface them.
    bool hide_shorts{true};
    bool hide_promoted{true};
    bool hide_shopping{true};
};

bool should_hide(const ContentItem& item, const FilterPolicy& policy = {});

}  // namespace ttnx::core
