#include "tizentube_nx/youtube/search_response.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const ttnx::core::BrowseResult* find_result(
    const ttnx::core::BrowsePage& page,
    const std::string& id) {
    for (const auto& result : page.results) {
        if (result.id == id) return &result;
    }
    return nullptr;
}

std::string scoped_page(std::string items) {
    return std::string(R"JSON({"contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{"sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":)JSON") +
        items + R"JSON(}}]}}}}})JSON";
}

}  // namespace

int main() {
    using ttnx::youtube::parse_scoped_search_response;

    const auto parsed = parse_scoped_search_response(scoped_page(R"JSON([
      {"videoRenderer":{
        "videoId":"BADGROUP001",
        "title":{"simpleText":"Bad grouping"},
        "viewCountText":{"simpleText":"1,23,456 views"}
      }},
      {"videoRenderer":{
        "videoId":"OVERFLOW001",
        "title":{"simpleText":"Overflow count"},
        "viewCountText":{"simpleText":"18446744073709551616 views"}
      }},
      {"channelRenderer":{
        "channelId":"UCBADCOUNT",
        "title":{"simpleText":"Bad subscriber count"},
        "subscriberCountText":{"simpleText":"12.3K subscribers"},
        "videoCountText":{"simpleText":"1,00 videos"}
      }},
      {"playlistRenderer":{
        "playlistId":"PLLOCALCOUNT",
        "title":{"simpleText":"Localized playlist"},
        "videoCountText":{"simpleText":"1.234 Videos"}
      }},
      {"videoRenderer":{
        "videoId":"BADSTART001",
        "title":{"simpleText":"Upcoming without exact start"},
        "upcomingEventData":{"startTime":"tomorrow-ish"},
        "badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_UPCOMING"}}]
      }},
      {"videoRenderer":{
        "videoId":"CONFLICTST1",
        "title":{"simpleText":"Upcoming conflicting starts"},
        "upcomingEventData":{"startTime":"1700000000"},
        "secondary":{"startTime":"1700000001"},
        "badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_UPCOMING"}}]
      }}
    ])JSON"));

    expect(parsed.page.has_value(), "numeric/timing edge fixture parses");
    if (parsed.page) {
        const auto* bad_group = find_result(*parsed.page, "BADGROUP001");
        expect(bad_group && bad_group->view_count_text == "1,23,456 views",
               "malformed grouped count text is retained for presentation");
        expect(bad_group && !bad_group->view_count,
               "malformed grouped count is not converted to an exact number");

        const auto* overflow = find_result(*parsed.page, "OVERFLOW001");
        expect(overflow && overflow->view_count_text == "18446744073709551616 views",
               "overflow count text remains available");
        expect(overflow && !overflow->view_count,
               "overflow exact count fails closed without wrapping");

        const auto* channel = find_result(*parsed.page, "UCBADCOUNT");
        expect(channel && channel->subscriber_count_text == "12.3K subscribers",
               "abbreviated subscriber text is retained");
        expect(channel && !channel->subscriber_count,
               "abbreviated subscriber count is not invented as exact");
        expect(channel && channel->video_count_text == "1,00 videos" && !channel->video_count,
               "malformed channel video grouping is text-only");

        const auto* playlist = find_result(*parsed.page, "PLLOCALCOUNT");
        expect(playlist && playlist->video_count_text == "1.234 Videos",
               "localized playlist count text is retained");
        expect(playlist && !playlist->video_count,
               "localized playlist count is not guessed as exact");

        const auto* bad_start = find_result(*parsed.page, "BADSTART001");
        expect(bad_start && bad_start->upcoming,
               "upcoming marker survives malformed optional start time");
        expect(bad_start && !bad_start->scheduled_start_time_seconds,
               "malformed scheduled start time is ignored");

        const auto* conflicting = find_result(*parsed.page, "CONFLICTST1");
        expect(conflicting && conflicting->upcoming,
               "conflicting scheduled values do not invalidate upcoming result");
        expect(conflicting && !conflicting->scheduled_start_time_seconds,
               "conflicting scheduled start values are not guessed");
    }

    if (failures == 0) {
        std::cout << "All Search metadata numeric edge tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
