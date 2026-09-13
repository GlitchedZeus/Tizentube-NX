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

const ttnx::core::BrowseResult* result_by_id(
    const ttnx::core::BrowsePage& page,
    const std::string& id) {
    for (const auto& result : page.results) {
        if (result.id == id) return &result;
    }
    return nullptr;
}

}  // namespace

int main() {
    using ttnx::core::BrowseResultKind;
    using ttnx::youtube::parse_scoped_search_response;

    const auto empty = parse_scoped_search_response(R"JSON({
      "contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{
        "sectionListRenderer":{"contents":[]}
      }}}
    })JSON");
    expect(empty.page.has_value(), "empty recognized Search container is accepted");
    if (empty.page) {
        expect(empty.page->items.empty() && empty.page->results.empty(),
               "empty Search container yields no visible results");
    }

    const auto odd = parse_scoped_search_response(R"JSON({
      "contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{
        "sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[
          {"videoRenderer":{"title":{"simpleText":"Missing ID"}}},
          {"videoRenderer":{"videoId":"MISS_TITLE1"}},
          {"videoRenderer":{
            "videoId":"RUNSTITLE01",
            "title":{"runs":[{"text":"Alternate "},{"text":"Title"}]},
            "longBylineText":{"runs":[{"text":"Runs Channel"}]},
            "thumbnail":{"thumbnails":[{"url":"not-a-url"}]},
            "lengthText":{"simpleText":"not-a-time"}
          }},
          {"videoRenderer":{
            "videoId":"NOCHANNEL01",
            "title":{"simpleText":"No channel metadata"},
            "thumbnail":{"thumbnails":[{"url":"https://i.ytimg.com/valid.jpg"}]},
            "lengthText":{"simpleText":"3:21"}
          }},
          {"videoRenderer":{
            "videoId":"LIVENOW0001",
            "title":{"simpleText":"Live now"},
            "badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_LIVE_NOW"}}]
          }},
          {"videoRenderer":{
            "videoId":"UPCOMING001",
            "title":{"simpleText":"Upcoming stream"},
            "upcomingEventData":{"startTime":"9999999999"}
          }},
          {"videoRenderer":{
            "videoId":"PRIVATE0001",
            "title":{"simpleText":"Private video"}
          }},
          {"videoRenderer":{
            "videoId":"DUPLICATE01",
            "title":{"simpleText":"First copy"}
          }},
          {"videoRenderer":{
            "videoId":"DUPLICATE01",
            "title":{"simpleText":"Second copy"}
          }},
          {"channelRenderer":{
            "channelId":"UCROBUSTNESSCHANNEL000001",
            "title":{"simpleText":"Robustness Channel"},
            "thumbnail":{"thumbnails":[{"url":"https://yt3.ggpht.com/robust"}]},
            "subscriberCountText":{"simpleText":"12K subscribers"},
            "videoCountText":{"simpleText":"87 videos"}
          }},
          {"playlistRenderer":{
            "playlistId":"PLRobustness123",
            "title":{"simpleText":"Robustness Playlist"},
            "shortBylineText":{"simpleText":"Playlist Owner"},
            "videoCountText":{"simpleText":"22 videos"}
          }},
          {"mysteryFutureRenderer":{
            "videoId":"UNKNOWN0001",
            "title":{"simpleText":"Must fail closed"}
          }}
        ]}}]}
      }}}
    })JSON");

    expect(odd.page.has_value(), "odd-but-structurally-valid Search page parses");
    if (odd.page) {
        const auto& page = *odd.page;
        expect(result_by_id(page, "MISS_TITLE1") == nullptr, "renderer missing title is dropped");
        expect(result_by_id(page, "UNKNOWN0001") == nullptr,
               "unknown future renderer fails closed without killing siblings");

        const auto* runs = result_by_id(page, "RUNSTITLE01");
        expect(runs != nullptr, "alternate title runs structure is supported");
        if (runs) {
            expect(runs->title == "Alternate Title", "title runs concatenate deterministically");
            expect(runs->channel_name == "Runs Channel", "available channel text is preserved");
            expect(runs->thumbnail_url.empty(), "malformed thumbnail is removed from normalized output");
            expect(runs->duration_text.empty(), "invalid duration text is removed from normalized output");
        }

        const auto* no_channel = result_by_id(page, "NOCHANNEL01");
        expect(no_channel != nullptr, "video can survive when channel metadata is omitted");
        if (no_channel) {
            expect(no_channel->channel_name.empty(), "missing channel is not invented");
            expect(no_channel->thumbnail_url == "https://i.ytimg.com/valid.jpg",
                   "valid HTTPS thumbnail is retained");
            expect(no_channel->duration_text == "3:21", "valid duration is retained");
        }

        const auto* live = result_by_id(page, "LIVENOW0001");
        expect(live && live->kind == BrowseResultKind::Video && live->live,
               "recognized live shape becomes a video with live indicator");

        const auto* upcoming = result_by_id(page, "UPCOMING001");
        expect(upcoming != nullptr, "upcoming-shaped normal video remains usable");
        if (upcoming) {
            expect(!upcoming->live, "upcoming shape is not fabricated as live");
        }

        expect(result_by_id(page, "PRIVATE0001") != nullptr,
               "private-looking titled result remains representable without invented metadata");

        int duplicate_count = 0;
        for (const auto& result : page.results) {
            if (result.id == "DUPLICATE01") ++duplicate_count;
        }
        expect(duplicate_count == 1, "duplicate legitimate renderer is deterministically deduplicated");
        const auto* duplicate = result_by_id(page, "DUPLICATE01");
        expect(duplicate && duplicate->title == "First copy", "dedupe is stable first-wins");

        const auto* channel = result_by_id(page, "UCROBUSTNESSCHANNEL000001");
        expect(channel && channel->kind == BrowseResultKind::Channel,
               "mixed page retains channel result");
        const auto* playlist = result_by_id(page, "PLRobustness123");
        expect(playlist && playlist->kind == BrowseResultKind::Playlist,
               "mixed page retains playlist result");
    }

    const auto no_token = parse_scoped_search_response(R"JSON({
      "onResponseReceivedCommands":[{"appendContinuationItemsAction":{
        "continuationItems":[{"videoRenderer":{
          "videoId":"NOTOKEN0001","title":{"simpleText":"No next token"}
        }}]
      }}]
    })JSON");
    expect(no_token.page.has_value(), "continuation payload without a next token remains usable");
    if (no_token.page) {
        expect(no_token.page->continuation.empty(), "missing continuation token means no further page");
        expect(result_by_id(*no_token.page, "NOTOKEN0001") != nullptr,
               "continuation result survives without pagination token");
    }

    const auto modern_token = parse_scoped_search_response(R"JSON({
      "onResponseReceivedActions":[{"reloadContinuationItemsCommand":{
        "continuationItems":[
          {"videoRenderer":{"videoId":"MODERNTOK01","title":{"simpleText":"Modern token"}}},
          {"continuationItemViewModel":{"continuationCommand":{"token":"OPAQUE-NEXT"}}}
        ]
      }},{"unrelatedAction":{"videoRenderer":{
        "videoId":"OUTSIDE0001","title":{"simpleText":"Must not escape"}
      }}}]
    })JSON");
    expect(modern_token.page.has_value(), "recognized modern continuation action parses");
    if (modern_token.page) {
        expect(modern_token.page->continuation == "OPAQUE-NEXT",
               "recognized modern continuation token is preserved opaquely");
        expect(result_by_id(*modern_token.page, "OUTSIDE0001") == nullptr,
               "unrelated continuation action cannot leak renderer-shaped data");
    }

    const auto opaque_blocked = parse_scoped_search_response(R"JSON({
      "topbar":{"videoRenderer":{"videoId":"TOPBAR00001","title":{"simpleText":"No"}}},
      "contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{
        "sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[
          {"adSlotRenderer":{"content":{"videoRenderer":{
            "videoId":"ADNESTED001","title":{"simpleText":"No ad escape"}
          }}}},
          {"reelShelfRenderer":{"items":[{"videoRenderer":{
            "videoId":"SHORTNEST01","title":{"simpleText":"No short escape"}
          }}]}},
          {"videoRenderer":{"videoId":"LEGITSIB001","title":{"simpleText":"Legit sibling"}}}
        ]}}]}
      }}}
    })JSON");
    expect(opaque_blocked.page.has_value(), "blocked-subtree fixture parses");
    if (opaque_blocked.page) {
        expect(result_by_id(*opaque_blocked.page, "LEGITSIB001") != nullptr,
               "legitimate sibling survives blocked subtrees");
        expect(result_by_id(*opaque_blocked.page, "ADNESTED001") == nullptr,
               "renderer inside ad subtree cannot escape");
        expect(result_by_id(*opaque_blocked.page, "SHORTNEST01") == nullptr,
               "renderer inside Shorts subtree cannot escape");
        expect(result_by_id(*opaque_blocked.page, "TOPBAR00001") == nullptr,
               "renderer outside Search scope cannot escape");
    }

    if (failures == 0) {
        std::cout << "All Search robustness corpus tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
