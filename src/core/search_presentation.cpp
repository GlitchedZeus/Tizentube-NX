#include "tizentube_nx/core/search_presentation.hpp"

#include "tizentube_nx/core/url.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ttnx::core {
namespace {

std::string grouped_decimal(std::uint64_t value) {
    auto digits = std::to_string(value);
    for (std::size_t pos = digits.size(); pos > 3; pos -= 3) {
        digits.insert(pos - 3, 1, ',');
    }
    return digits;
}

void append_part(std::vector<std::string>& parts, std::string value) {
    if (!value.empty()) parts.push_back(std::move(value));
}

std::string join_parts(const std::vector<std::string>& parts) {
    std::string output;
    for (const auto& part : parts) {
        if (!output.empty()) output += "  •  ";
        output += part;
    }
    return output;
}

std::optional<std::string> canonical_result_url(const BrowseResult& result) {
    switch (result.kind) {
        case BrowseResultKind::Video:
            return canonical_watch_url(result.id);
        case BrowseResultKind::Channel:
            return canonical_channel_url(result.id);
        case BrowseResultKind::Playlist:
            return canonical_playlist_url(result.id);
    }
    return std::nullopt;
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

std::string subscriber_count_display(const BrowseResult& result) {
    if (!result.subscriber_count_text.empty()) return result.subscriber_count_text;
    if (!result.subscriber_count) return {};
    return grouped_decimal(*result.subscriber_count) +
           (*result.subscriber_count == 1 ? " subscriber" : " subscribers");
}

std::string video_count_display(const BrowseResult& result) {
    if (!result.video_count_text.empty()) return result.video_count_text;
    if (!result.video_count) return {};
    return grouped_decimal(*result.video_count) + (*result.video_count == 1 ? " video" : " videos");
}

std::string search_result_kind_label(BrowseResultKind kind) {
    switch (kind) {
        case BrowseResultKind::Video:
            return "VIDEO";
        case BrowseResultKind::Channel:
            return "CHANNEL";
        case BrowseResultKind::Playlist:
            return "PLAYLIST";
    }
    return "RESULT";
}

std::string search_result_metadata_display(const BrowseResult& result) {
    std::vector<std::string> parts;
    parts.reserve(5);

    switch (result.kind) {
        case BrowseResultKind::Video:
            append_part(parts, live_status_label(result));
            append_part(parts, result.channel_name);
            append_part(parts, duration_display(result));
            append_part(parts, view_count_display(result));
            append_part(parts, upload_age_display(result));
            break;
        case BrowseResultKind::Channel:
            append_part(parts, subscriber_count_display(result));
            append_part(parts, video_count_display(result));
            break;
        case BrowseResultKind::Playlist:
            append_part(parts, result.channel_name);
            append_part(parts, video_count_display(result));
            break;
    }

    return join_parts(parts);
}

std::string search_result_selection_display(const BrowseResult& result) {
    std::string output = "Selected " + search_result_kind_label(result.kind) + ": " + result.title;
    if (!result.id.empty()) output += "\nID: " + result.id;
    if (const auto url = canonical_result_url(result)) output += "\nURL: " + *url;
    output += "\nPlayback is not enabled in this milestone.";
    return output;
}

}  // namespace ttnx::core
