#pragma once

#include "tizentube_nx/core/search_result.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace ttnx::core {

// Pure renderer-independent display helpers. These never inspect YouTube JSON
// and are safe for host tests and future Borealis presentation code.
[[nodiscard]] std::string format_duration_seconds(std::uint64_t seconds);
[[nodiscard]] std::string duration_display(const BrowseResult& result);
[[nodiscard]] std::string live_status_label(const BrowseResult& result);
[[nodiscard]] std::string view_count_display(const BrowseResult& result);
[[nodiscard]] std::string upload_age_display(const BrowseResult& result);
[[nodiscard]] std::string video_count_display(const BrowseResult& result);

}  // namespace ttnx::core
