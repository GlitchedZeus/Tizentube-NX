#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/core/search_result.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

ttnx::core::RendererRecord video(
    std::string id,
    std::string title,
    ttnx::core::ContentKind kind = ttnx::core::ContentKind::Video) {
    ttnx::core::RendererRecord record;
    record.renderer = "videoRenderer";
    record.item.kind = kind;
    record.item.id = std::move(id);
    record.item.title = std::move(title);
    return record;
}

}  // namespace

int main() {
    using namespace ttnx::core;

    auto rich = video("ABCDEFGHIJK", "Normalized video");
    rich.channel_title = "Example Channel";
    rich.channel_id = "UCExampleChannel";
    rich.thumbnail_url = "https://i.ytimg.com/example.jpg";
    rich.duration_text = "12:34";
    rich.view_count_text = "1.2M views";
    rich.published_text = "2 years ago";
    rich.accessibility_text = "Normalized video by Example Channel";

    auto channel = RendererRecord{};
    channel.renderer = "channelRenderer";
    channel.item.kind = ContentKind::Channel;
    channel.item.id = "UCExampleChannel";
    channel.item.title = "Example Channel";
    channel.thumbnail_url = "https://yt3.ggpht.com/example";
    channel.subscriber_count_text = "123K subscribers";
    channel.video_count_text = "400 videos";

    auto playlist = RendererRecord{};
    playlist.renderer = "playlistRenderer";
    playlist.item.kind = ContentKind::Playlist;
    playlist.item.id = "PLExampleList";
    playlist.item.title = "Example Playlist";
    playlist.channel_title = "Example Channel";
    playlist.channel_id = "UCExampleChannel";
    playlist.video_count_text = "42 videos";

    auto live = video("LMNOPQRSTUV", "Live now", ContentKind::Live);

    const auto page = sanitize_guest_page(
        {rich, rich, channel, playlist, live},
        "NEXT");

    expect(page.items.size() == 4, "duplicate renderer records are removed deterministically");
    expect(page.results.size() == 4, "normalized result list mirrors deduplicated visible records");
    expect(page.continuation == "NEXT", "dedupe does not alter continuation token");

    if (page.results.size() >= 4) {
        const auto& normalized = page.results[0];
        expect(normalized.kind == BrowseResultKind::Video, "video normalizes to explicit Video result type");
        expect(normalized.id == "ABCDEFGHIJK", "video id preserved");
        expect(normalized.title == "Normalized video", "title preserved");
        expect(normalized.channel_name == "Example Channel", "channel name preserved");
        expect(normalized.channel_id == "UCExampleChannel", "channel id preserved when available");
        expect(normalized.duration_text == "12:34", "duration preserved");
        expect(normalized.thumbnail_url == "https://i.ytimg.com/example.jpg", "thumbnail preserved");
        expect(normalized.view_count_text == "1.2M views", "view-count text preserved");
        expect(normalized.published_text == "2 years ago", "published text preserved");
        expect(normalized.accessibility_text == "Normalized video by Example Channel",
               "accessibility metadata preserved");

        expect(page.results[1].kind == BrowseResultKind::Channel, "channel has explicit result type");
        expect(page.results[1].subscriber_count_text == "123K subscribers",
               "channel subscriber text preserved");
        expect(page.results[2].kind == BrowseResultKind::Playlist, "playlist has explicit result type");
        expect(page.results[2].video_count_text == "42 videos", "playlist video count preserved");
        expect(page.results[3].kind == BrowseResultKind::Video && page.results[3].live,
               "live renderer normalizes as a video with a live indicator");
    }

    BrowseResult same_id_video;
    same_id_video.kind = BrowseResultKind::Video;
    same_id_video.id = "same";
    BrowseResult same_id_channel;
    same_id_channel.kind = BrowseResultKind::Channel;
    same_id_channel.id = "same";
    expect(browse_result_identity(same_id_video) == "video:same", "video identity is type-qualified");
    expect(browse_result_identity(same_id_channel) == "channel:same", "channel identity is type-qualified");
    expect(browse_result_identity(same_id_video) != browse_result_identity(same_id_channel),
           "different entity types cannot alias each other");

    auto disguised_short = video("SAMEVIDEO01", "Blocked short");
    disguised_short.item.kind = ContentKind::Short;
    auto legitimate = video("SAMEVIDEO01", "Legitimate normal video");
    const auto short_then_video = sanitize_guest_page({disguised_short, legitimate}, {});
    expect(short_then_video.results.size() == 1,
           "blocked Short does not consume dedupe identity for a legitimate sibling");
    if (!short_then_video.results.empty()) {
        expect(short_then_video.results[0].title == "Legitimate normal video",
               "dedupe runs only after the Shorts firewall");
    }

    if (failures == 0) {
        std::cout << "All normalized Search result tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
