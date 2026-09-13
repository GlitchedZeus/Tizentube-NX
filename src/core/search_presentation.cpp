#include "tizentube_nx/core/search_presentation.hpp"

#include <string>

namespace ttnx::core {
namespace {

std::string grouped_decimal(std::uint64_t value) {
    auto digits = std::to_string(value);
    for (std::size_t pos = digits.size(); pos > 3; pos -= 3) {
        digits.insert(pos - 3, 1, ',');
    }
    return digits;
}

}  // namespace

std::string format_duration_seconds(std::uint64_t seconds) {
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainder = seconds % 60;

    std::string output;
    if (hours > 0) {
        output = std::to_string(hours) + ':';
        if (minutes < 10) output.push_back('0');
        output += std::to_string(minutes) + ':';
    } else {
        output = std::to_string(minutes) + ':';
    }
    if (remainder < 10) output.push_back('0');
    output += std::to_string(remainder);
    return output;
}

std::string duration_display(const BrowseResult& result) {
    if (!result.duration_text.empty()) return result.duration_text;
    return result.duration_seconds ? format_duration_seconds(*result.duration_seconds) : std::string{};
}

std::string live_status_label(const BrowseResult& result) {
    if (result.live) return "LIVE";
    if (result.upcoming) return "UPCOMING";
    return {};
}

std::string view_count_display(const BrowseResult& result) {
    if (!result.view_count_text.empty()) return result.view_count_text;
    if (!result.view_count) return {};
    return grouped_decimal(*result.view_count) + (*result.view_count == 1 ? " view" : " views");
}

std::string upload_age_display(const BrowseResult& result) {
    return result.published_text;
}

std::string video_count_display(const BrowseResult& result) {
    if (!result.video_count_text.empty()) return result.video_count_text;
    if (!result.video_count) return {};
    return grouped_decimal(*result.video_count) + (*result.video_count == 1 ? " video" : " videos");
}

}  // namespace ttnx::core
