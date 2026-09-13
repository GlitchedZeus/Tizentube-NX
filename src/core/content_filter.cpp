#include "tizentube_nx/core/content_filter.hpp"

namespace ttnx::core {

bool should_hide(const ContentItem& item, const FilterPolicy& policy) {
    if (item.kind == ContentKind::Short) return true; // hard project invariant
    if (policy.hide_promoted && item.promoted) return true;
    if (policy.hide_shopping && item.shopping) return true;
    return false;
}

}  // namespace ttnx::core
