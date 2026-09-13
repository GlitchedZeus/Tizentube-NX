#include "tizentube_nx/core/search_presentation.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

}  // namespace

int main() {
    using namespace ttnx::core;

    expect(format_duration_seconds(0) == "0:00", "zero duration formats safely");
    expect(format_duration_seconds(65) == "1:05", "minute duration formats safely");
    expect(format_duration_seconds(3661) == "1:01:01", "hour duration formats safely");
    expect(format_duration_seconds(360000) == "100:00:00", "long duration remains deterministic");

    BrowseResult result;
    expect(duration_display(result).empty(), "missing duration displays as unknown/empty");
    result.duration_seconds = 125;
    expect(duration_display(result) == "2:05", "numeric duration provides fallback display");
    result.duration_text = "02:05";
    expect(duration_display(result) == "02:05", "server presentation text wins when safely retained");

    expect(live_status_label(result).empty(), "ordinary VOD has no live label");
    result.upcoming = true;
    expect(live_status_label(result) == "UPCOMING", "upcoming result has upcoming label");
    result.live = true;
    expect(live_status_label(result) == "LIVE", "live state takes priority over upcoming label");

    result.view_count_text.clear();
    result.view_count = 1234567;
    expect(view_count_display(result) == "1,234,567 views", "exact numeric view count formats with grouping");
    result.view_count_text = "1.2M views";
    expect(view_count_display(result) == "1.2M views", "retained localized/abbreviated view text is not rewritten");

    result.published_text = "3 days ago";
    expect(upload_age_display(result) == "3 days ago", "upload-age helper preserves safe source text");
    result.published_text.clear();
    expect(upload_age_display(result).empty(), "missing upload age remains unknown");

    result.video_count_text.clear();
    result.video_count = 1;
    expect(video_count_display(result) == "1 video", "singular video count formats correctly");
    result.video_count = 2048;
    expect(video_count_display(result) == "2,048 videos", "plural video count formats correctly");
    result.video_count_text = "2K videos";
    expect(video_count_display(result) == "2K videos", "source video-count text takes precedence");

    if (failures == 0) {
        std::cout << "All Search presentation helper tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
