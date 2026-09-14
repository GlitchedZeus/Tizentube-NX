#pragma once

#include "tizentube_nx/core/guest_browse.hpp"

#include <optional>
#include <string>

namespace ttnx::youtube {

struct BrowseResponseParseResult {
    std::optional<core::BrowsePage> page;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page.has_value();
    }
};

}  // namespace ttnx::youtube
