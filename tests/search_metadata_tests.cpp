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
    using ttnx::core::BrowseResultKind;
    using ttnx::youtube::parse_scoped_search_response;

    const std::string legacy_items = R"JSON([
      {"videoRenderer":{
        "videoId":"LEGACYV001",
        "title":{"runs":[{"text":"Legacy VOD"}],"accessibility":{"accessibilityData":{"label":"Legacy VOD accessibility"}}},
        "longBylineText":{"runs":[{"text":"Legacy Channel","navigationEndpoint":{"browseEndpoint":{"browseId":"UCLEGACY"}}}]},
        "thumbnail":{"thumbnails":[
          {"url":"https://i.ytimg.com/legacy-small.jpg","width":120,"height":90},
          {"url":"https://i.ytimg.com/legacy-large.jpg","width":640,"height":360},
          {"url":"http://example.invalid/not-https.jpg","width":1280,"height":720}
        ]},
        "lengthText":{"simpleText":"12:34"},
        "viewCountText":{"simpleText":"1,234 views"},
        "publishedTimeText":{"simpleText":"2 days ago"}
      }},
      {"videoRenderer":{
        "videoId":"LIVEVID0001",
        "title":{"simpleText":"Live Video"},
        "badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_LIVE_NOW"}}]
      }},
      {"videoRenderer":{
        "videoId":"UPCOMING001",
        "title":{"simpleText":"Upcoming Video"},
        "upcomingEventData":{"startTime":"1700000000"},
        "badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_UPCOMING"}}]
      }},
      {"channelRenderer":{
        "channelId":"UCCHAN123",
        "title":{"simpleText":"Channel Result","accessibility":{"accessibilityData":{"label":"Channel accessible"}}},
        "thumbnail":{"thumbnails":[{"url":"https://yt3.ggpht.com/channel","width":176,"height":176}]},
        "subscriberCountText":{"simpleText":"12,345 subscribers"},
        "videoCountText":{"simpleText":"321 videos"}
      }},
      {"playlistRenderer":{
        "playlistId":"PLMETA123",
        "title":{"simpleText":"Playlist Result","accessibility":{"accessibilityData":{"label":"Playlist accessible"}}},
        "shortBylineText":{"runs":[{"text":"Playlist Owner","navigationEndpoint":{"browseEndpoint":{"browseId":"UCOWNER"}}}]},
        "videoCountText":{"simpleText":"42 videos"},
        "thumbnail":{"thumbnails":[{"url":"https://i.ytimg.com/playlist.jpg","width":480,"height":270}]}
      }},
      {"videoRenderer":{
        "videoId":"MISSINGMETA",
        "title":{"simpleText":"Still Valid"}
      }},
      {"videoRenderer":{
        "videoId":"LOCALCOUNT1",
        "title":{"simpleText":"Localized count"},
        "viewCountText":{"simpleText":"1.234 Aufrufe"}
      }},
      {"videoRenderer":{
        "videoId":"AMBIGCHAN01",
        "title":{"simpleText":"Ambiguous channel"},
        "longBylineText":{"runs":[{"text":"Owner A","navigationEndpoint":{"browseEndpoint":{"browseId":"UCA"}}}]},
        "shortBylineText":{"runs":[{"text":"Owner B","navigationEndpoint":{"browseEndpoint":{"browseId":"UCB"}}}]}
      }},
      {"videoRenderer":{
        "videoId":"WRONGENDPT1",
        "title":{"simpleText":"Unexpected endpoint"},
        "longBylineText":{"runs":[{"text":"Owner","navigationEndpoint":{"watchEndpoint":{"videoId":"OTHER"}}}]}
      }},
      {"videoRenderer":{
        "videoId":"BLOCKSHORT1",
        "title":{"simpleText":"Blocked short"},
        "viewCountText":{"simpleText":"999,999 views"},
        "navigationEndpoint":{"reelWatchEndpoint":{"videoId":"BLOCKSHORT1"}}
      }},
      {"adSlotRenderer":{"content":{"videoRenderer":{
        "videoId":"BLOCKAD0001","title":{"simpleText":"Blocked ad"},
        "viewCountText":{"simpleText":"888 views"}
      }}}},
      {"productRenderer":{"content":{"videoRenderer":{
        "videoId":"BLOCKSHOP01","title":{"simpleText":"Blocked product"},
        "viewCountText":{"simpleText":"777 views"}
      }}}}
    ])JSON";

    const auto legacy = parse_scoped_search_response(scoped_page(legacy_items));
    expect(legacy.page.has_value(), "legacy metadata fixture parses");
    if (legacy.page) {
        const auto* vod = find_result(*legacy.page, "LEGACYV001");
        expect(vod != nullptr, "legacy VOD survives normalization");
        if (vod) {
            expect(vod->kind == BrowseResultKind::Video, "legacy VOD normalized as video");
            expect(vod->channel_name == "Legacy Channel", "legacy channel name extracted");
            expect(vod->channel_id == "UCLEGACY", "legacy channel id extracted from byline endpoint");
            expect(vod->duration_text == "12:34", "legacy duration text retained");
            expect(vod->duration_seconds && *vod->duration_seconds == 754,
                   "legacy duration parsed deterministically");
            expect(vod->view_count_text == "1,234 views", "legacy view-count text retained");
            expect(vod->view_count && *vod->view_count == 1234,
                   "exact legacy view count parsed");
            expect(vod->published_text == "2 days ago", "legacy upload-age text retained");
            expect(vod->accessibility_text == "Legacy VOD accessibility",
                   "legacy accessibility label extracted");
            expect(vod->thumbnails.size() == 2, "invalid thumbnail candidate ignored");
            if (!vod->thumbnails.empty()) {
                expect(vod->thumbnails.front().url == "https://i.ytimg.com/legacy-large.jpg",
                       "thumbnail candidates use deterministic largest-first ordering");
                expect(vod->thumbnails.front().width == 640 && vod->thumbnails.front().height == 360,
                       "thumbnail dimensions retained");
                expect(vod->thumbnail_url == vod->thumbnails.front().url,
                       "preferred thumbnail mirrors first normalized candidate");
            }
        }

        const auto* live = find_result(*legacy.page, "LIVEVID0001");
        expect(live && live->live && !live->upcoming, "legacy live video marked live only");

        const auto* upcoming = find_result(*legacy.page, "UPCOMING001");
        expect(upcoming && upcoming->upcoming && !upcoming->live,
               "legacy upcoming video marked upcoming only");
        if (upcoming) {
            expect(upcoming->scheduled_start_time_seconds &&
                   *upcoming->scheduled_start_time_seconds == 1700000000ULL,
                   "upcoming start time parsed when exact and bounded");
        }

        const auto* channel = find_result(*legacy.page, "UCCHAN123");
        expect(channel && channel->kind == BrowseResultKind::Channel,
               "legacy channel result survives");
        if (channel) {
            expect(channel->channel_id == "UCCHAN123", "channel id copied into normalized attribution slot");
            expect(channel->subscriber_count_text == "12,345 subscribers",
                   "channel subscriber text retained");
            expect(channel->subscriber_count && *channel->subscriber_count == 12345,
                   "exact subscriber count parsed");
            expect(channel->video_count && *channel->video_count == 321,
                   "exact channel video count parsed");
        }

        const auto* playlist = find_result(*legacy.page, "PLMETA123");
        expect(playlist && playlist->kind == BrowseResultKind::Playlist,
               "legacy playlist result survives");
        if (playlist) {
            expect(playlist->channel_name == "Playlist Owner", "playlist owner name extracted");
            expect(playlist->channel_id == "UCOWNER", "playlist owner id extracted");
            expect(playlist->video_count && *playlist->video_count == 42,
                   "playlist video count parsed when exact");
        }

        const auto* missing = find_result(*legacy.page, "MISSINGMETA");
        expect(missing != nullptr, "absent optional metadata does not invalidate result");
        if (missing) {
            expect(missing->channel_id.empty() && missing->view_count_text.empty() &&
                   missing->thumbnails.empty(), "absent optional metadata remains empty");
        }

        const auto* localized = find_result(*legacy.page, "LOCALCOUNT1");
        expect(localized && localized->view_count_text == "1.234 Aufrufe",
               "localized view-count text is retained");
        expect(localized && !localized->view_count,
               "localized/ambiguous count is not invented as an exact number");

        const auto* ambiguous = find_result(*legacy.page, "AMBIGCHAN01");
        expect(ambiguous && ambiguous->channel_id.empty(),
               "conflicting channel navigation endpoints disable channel id rather than guessing");

        const auto* wrong_endpoint = find_result(*legacy.page, "WRONGENDPT1");
        expect(wrong_endpoint && wrong_endpoint->channel_name == "Owner",
               "unexpected endpoint type does not discard safe owner text");
        expect(wrong_endpoint && wrong_endpoint->channel_id.empty(),
               "unexpected endpoint type is not treated as a channel id");

        expect(find_result(*legacy.page, "BLOCKSHORT1") == nullptr,
               "Short with valid-looking metadata remains blocked");
        expect(find_result(*legacy.page, "BLOCKAD0001") == nullptr,
               "ad subtree metadata cannot escape firewall");
        expect(find_result(*legacy.page, "BLOCKSHOP01") == nullptr,
               "shopping subtree metadata cannot escape firewall");
    }

    const std::string modern_items = R"JSON([
      {"lockupViewModel":{
        "contentType":"LOCKUP_CONTENT_TYPE_VIDEO",
        "contentId":"MODERNV001",
        "contentImage":{"thumbnailViewModel":{
          "image":{"sources":[
            {"url":"https://i.ytimg.com/modern-320.jpg","width":320,"height":180},
            {"url":"https://i.ytimg.com/modern-1280.jpg","width":1280,"height":720},
            {"url":"javascript:bad","width":9999,"height":9999}
          ]},
          "overlays":[{"thumbnailOverlayBadgeViewModel":{"text":{"content":"3:21"}}}]
        }},
        "metadata":{"lockupMetadataViewModel":{
          "title":{"content":"Modern VOD","accessibility":{"accessibilityData":{"label":"Modern accessible"}}},
          "metadata":{"contentMetadataViewModel":{"metadataRows":[
            {"metadataParts":[{"text":{"content":"Modern Channel","commandRuns":[{"onTap":{"innertubeCommand":{"browseEndpoint":{"browseId":"UCMODERN"}}}}]}}]},
            {"metadataParts":[{"text":{"content":"9,876 views"}},{"text":{"content":"3 hours ago"}}]}
          ]}}
        }}
      }},
      {"lockupViewModel":{
        "contentType":"LOCKUP_CONTENT_TYPE_PLAYLIST",
        "contentId":"PLMODERN1",
        "contentImage":{"collectionThumbnailViewModel":{"primaryThumbnail":{"thumbnailViewModel":{"image":{"sources":[{"url":"https://i.ytimg.com/plmodern.jpg","width":480,"height":270}]}}}}},
        "metadata":{"lockupMetadataViewModel":{
          "title":{"content":"Modern Playlist"},
          "metadata":{"contentMetadataViewModel":{"metadataRows":[
            {"metadataParts":[{"text":{"content":"Modern Owner","commandRuns":[{"onTap":{"innertubeCommand":{"browseEndpoint":{"browseId":"UCPLOWNER"}}}}]}}]},
            {"metadataParts":[{"text":{"content":"55 videos"}}]}
          ]}}
        }}
      }},
      {"lockupViewModel":{
        "contentType":"LOCKUP_CONTENT_TYPE_CHANNEL",
        "contentId":"UCMODCHAN",
        "contentImage":{"decoratedAvatarViewModel":{"avatar":{"avatarViewModel":{"image":{"sources":[{"url":"https://yt3.ggpht.com/modchan","width":176,"height":176}]}}}}},
        "metadata":{"lockupMetadataViewModel":{
          "title":{"content":"Modern Channel Result"},
          "metadata":{"contentMetadataViewModel":{"metadataRows":[
            {"metadataParts":[{"text":{"content":"20,000 subscribers"}}]},
            {"metadataParts":[{"text":{"content":"400 videos"}}]}
          ]}}
        }}
      }},
      {"lockupViewModel":{
        "contentType":"LOCKUP_CONTENT_TYPE_VIDEO",
        "contentId":"MODERNMISS1",
        "metadata":{"lockupMetadataViewModel":{"title":{"content":"Modern missing metadata"}}}
      }},
      {"lockupViewModel":{
        "contentType":"LOCKUP_CONTENT_TYPE_SHORT",
        "contentId":"MODERNSHORT",
        "metadata":{"lockupMetadataViewModel":{
          "title":{"content":"Still blocked"},
          "metadata":{"contentMetadataViewModel":{"metadataRows":[{"metadataParts":[{"text":{"content":"99,999 views"}}]}]}}
        }}
      }}
    ])JSON";

    const auto modern = parse_scoped_search_response(scoped_page(modern_items));
    expect(modern.page.has_value(), "modern metadata fixture parses");
    if (modern.page) {
        const auto* video = find_result(*modern.page, "MODERNV001");
        expect(video != nullptr, "modern video lockup survives");
        if (video) {
            expect(video->channel_name == "Modern Channel", "modern channel name extracted from metadata row");
            expect(video->channel_id == "UCMODERN", "modern channel id extracted from command run");
            expect(video->view_count && *video->view_count == 9876,
                   "modern exact view count parsed");
            expect(video->published_text == "3 hours ago", "modern upload-age text retained");
            expect(video->duration_seconds && *video->duration_seconds == 201,
                   "modern thumbnail overlay duration parsed");
            expect(video->accessibility_text == "Modern accessible",
                   "modern accessibility metadata retained");
            expect(video->thumbnails.size() == 2 && video->thumbnails.front().width == 1280,
                   "modern thumbnail candidates normalize deterministically");
        }

        const auto* playlist = find_result(*modern.page, "PLMODERN1");
        expect(playlist && playlist->channel_name == "Modern Owner",
               "modern playlist owner name extracted");
        expect(playlist && playlist->channel_id == "UCPLOWNER",
               "modern playlist owner id extracted");
        expect(playlist && playlist->video_count && *playlist->video_count == 55,
               "modern playlist exact video count parsed");

        const auto* channel = find_result(*modern.page, "UCMODCHAN");
        expect(channel && channel->kind == BrowseResultKind::Channel,
               "modern channel lockup is normalized");
        expect(channel && channel->subscriber_count && *channel->subscriber_count == 20000,
               "modern channel subscriber count parsed");
        expect(channel && channel->video_count && *channel->video_count == 400,
               "modern channel video count parsed");

        expect(find_result(*modern.page, "MODERNMISS1") != nullptr,
               "modern lockup with absent optional metadata remains valid");
        expect(find_result(*modern.page, "MODERNSHORT") == nullptr,
               "modern Short metadata remains blocked");
    }

    std::string long_metadata(5000, 'x');
    const auto long_optional = parse_scoped_search_response(scoped_page(
        "[{\"videoRenderer\":{\"videoId\":\"LONGMETA001\",\"title\":{\"simpleText\":\"Valid title\"},"
        "\"viewCountText\":{\"simpleText\":\"" + long_metadata + "\"}}}]"));
    expect(long_optional.page.has_value(), "extremely long optional metadata does not corrupt response");
    if (long_optional.page) {
        const auto* result = find_result(*long_optional.page, "LONGMETA001");
        expect(result != nullptr, "result survives overlong optional metadata");
        expect(result && result->view_count_text.empty() && !result->view_count,
               "overlong optional metadata is discarded rather than exposed");
    }

    const auto malformed_optional = parse_scoped_search_response(scoped_page(R"JSON([
      {"videoRenderer":{
        "videoId":"BADMETA0001",
        "title":{"simpleText":"Valid result"},
        "viewCountText":{"runs":42},
        "publishedTimeText":true,
        "thumbnail":{"thumbnails":[{"url":7,"width":"wide","height":null}]}
      }},
      {"playlistRenderer":{
        "playlistId":"PLNOCOUNT1",
        "title":{"simpleText":"No count"}
      }},
      {"channelRenderer":{
        "channelId":"UCNOSUBS1",
        "title":{"simpleText":"No subscribers"}
      }}
    ])JSON"));
    expect(malformed_optional.page.has_value(), "malformed optional metadata fixture parses");
    if (malformed_optional.page) {
        const auto* video = find_result(*malformed_optional.page, "BADMETA0001");
        expect(video != nullptr, "malformed optional metadata does not invalidate video");
        expect(video && video->view_count_text.empty() && video->thumbnails.empty(),
               "malformed optional fields are ignored");
        const auto* playlist = find_result(*malformed_optional.page, "PLNOCOUNT1");
        expect(playlist && !playlist->video_count && playlist->video_count_text.empty(),
               "playlist missing count remains valid with unknown count");
        const auto* channel = find_result(*malformed_optional.page, "UCNOSUBS1");
        expect(channel && !channel->subscriber_count && channel->subscriber_count_text.empty(),
               "channel missing subscriber count remains valid");
    }

    if (failures == 0) {
        std::cout << "All normalized Search metadata tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
