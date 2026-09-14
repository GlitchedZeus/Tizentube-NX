#include "tizentube_nx/core/search_presentation.hpp"

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

    result.subscriber_count_text.clear();
    result.subscriber_count = 1;
    expect(subscriber_count_display(result) == "1 subscriber", "singular subscriber count formats correctly");
    result.subscriber_count = 1234567;
    expect(subscriber_count_display(result) == "1,234,567 subscribers", "subscriber count groups digits");
    result.subscriber_count_text = "1.2M subscribers";
    expect(subscriber_count_display(result) == "1.2M subscribers", "source subscriber text takes precedence");

    result.video_count_text.clear();
    result.video_count = 1;
    expect(video_count_display(result) == "1 video", "singular video count formats correctly");
    result.video_count = 2048;
    expect(video_count_display(result) == "2,048 videos", "plural video count formats correctly");
    result.video_count_text = "2K videos";
    expect(video_count_display(result) == "2K videos", "source video-count text takes precedence");

    expect(search_result_kind_label(BrowseResultKind::Video) == "VIDEO", "video kind label is stable");
    expect(search_result_kind_label(BrowseResultKind::Channel) == "CHANNEL", "channel kind label is stable");
    expect(search_result_kind_label(BrowseResultKind::Playlist) == "PLAYLIST", "playlist kind label is stable");

    BrowseResult video;
    video.kind = BrowseResultKind::Video;
    video.id = "IzJ7R4EnYmI";
    video.title = "Normal video";
    video.channel_name = "Uploader";
    video.duration_text = "12:34";
    video.view_count_text = "42K views";
    video.published_text = "2 days ago";
    expect(search_result_metadata_display(video) ==
               "Uploader  •  12:34  •  42K views  •  2 days ago",
           "video card composes only available normalized metadata");
    video.live = true;
    expect(search_result_metadata_display(video).starts_with("LIVE  •  Uploader"),
           "live video card exposes LIVE before other metadata");
    expect(search_result_selection_display(video) ==
               "Selected VIDEO: Normal video\nID: IzJ7R4EnYmI\nURL: https://www.youtube.com/watch?v=IzJ7R4EnYmI\nPlayback is not enabled in this milestone.",
           "video selection seam uses exact clean canonical URL");

    BrowseResult channel;
    channel.kind = BrowseResultKind::Channel;
    channel.id = "UCabcdefghijklmnopqrstuv";
    channel.title = "Example channel";
    channel.subscriber_count_text = "1.2M subscribers";
    channel.video_count_text = "400 videos";
    expect(search_result_metadata_display(channel) ==
               "1.2M subscribers  •  400 videos",
           "channel card exposes subscriber and video counts");
    expect(search_result_selection_display(channel).find(
               "https://www.youtube.com/channel/UCabcdefghijklmnopqrstuv") != std::string::npos,
           "channel selection seam uses canonical stable channel URL");

    BrowseResult playlist;
    playlist.kind = BrowseResultKind::Playlist;
    playlist.id = "PL_Example-1234567890";
    playlist.title = "Example playlist";
    playlist.channel_name = "Playlist owner";
    playlist.video_count_text = "25 videos";
    expect(search_result_metadata_display(playlist) ==
               "Playlist owner  •  25 videos",
           "playlist card exposes owner and video count");
    expect(search_result_selection_display(playlist).find(
               "https://www.youtube.com/playlist?list=PL_Example-1234567890") != std::string::npos,
           "playlist selection seam uses canonical playlist URL");

    BrowseResult unknown;
    unknown.kind = BrowseResultKind::Video;
    unknown.title = "Sparse video";
    expect(search_result_metadata_display(unknown).empty(),
           "unknown optional metadata stays omitted instead of inventing zero values");
    expect(search_result_selection_display(unknown).find("URL:") == std::string::npos,
           "invalid or missing identity does not fabricate a canonical URL");

    if (failures == 0) {
        std::cout << "All Search presentation helper tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
