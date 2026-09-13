#include "tizentube_nx/core/content_filter.hpp"

namespace ttnx::core {

bool should_hide(const ContentItem& item, const FilterPolicy& policy) {
    // Hard project invariants: neither Shorts nor promoted/ad content may be
    // re-enabled by a future preference or malformed settings migration.
    if (item.kind == ContentKind::Short) return true;
    if (item.promoted) return true;
    if (policy.hide_shopping && item.shopping) return true;
    return false;
}

}  // namespace ttnx::core
