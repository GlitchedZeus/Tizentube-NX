#pragma once

#include "tizentube_nx/core/search_result.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace ttnx::core {

// Pure renderer-independent display helpers. These never inspect YouTube JSON
// and are safe for host tests and Borealis presentation code.
[[nodiscard]] std::string format_duration_seconds(std::uint64_t seconds);
[[nodiscard]] std::string duration_display(const BrowseResult& result);
[[nodiscard]] std::string live_status_label(const BrowseResult& result);
[[nodiscard]] std::string view_count_display(const BrowseResult& result);
[[nodiscard]] std::string upload_age_display(const BrowseResult& result);
[[nodiscard]] std::string subscriber_count_display(const BrowseResult& result);
[[nodiscard]] std::string video_count_display(const BrowseResult& result);

// Text-only Search cards intentionally do not use normalized thumbnail URLs;
// remote thumbnail hosts are not authorized during the live-Search milestone.
[[nodiscard]] std::string search_result_kind_label(BrowseResultKind kind);
[[nodiscard]] std::string search_result_metadata_display(const BrowseResult& result);

// Temporary no-playback selection seam. It exposes only normalized identity and
// canonical clean URLs generated locally from validated stable IDs.
[[nodiscard]] std::string search_result_selection_display(const BrowseResult& result);

}  // namespace ttnx::core
