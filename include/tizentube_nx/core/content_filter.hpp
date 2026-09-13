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
    // Non-negotiable project rule. Kept explicit in the policy object so tests
    // can guarantee it cannot silently regress during feed/parser refactors.
    bool hide_shorts{true};
    bool hide_promoted{true};
    bool hide_shopping{true};
};

bool should_hide(const ContentItem& item, const FilterPolicy& policy = {});

}  // namespace ttnx::core
