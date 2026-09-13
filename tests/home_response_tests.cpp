#include "tizentube_nx/youtube/home_response.hpp"

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

const ttnx::core::BrowseResult* find_id(
    const ttnx::core::HomePage& page,
    const std::string& id) {
    for (const auto& result : page.results) {
        if (result.id == id) return &result;
    }
    return nullptr;
}

bool has_section(const ttnx::core::HomePage& page, const std::string& title) {
    for (const auto& section : page.sections) {
        if (section.title == title) return true;
    }
    return false;
}

std::string home_fixture() {
    return R"JSON({
      "topbar": {
        "desktopTopbarRenderer": {
          "videoRenderer": {
            "videoId": "hostile-topbar-video",
            "title": {"simpleText": "Never Home content"}
          }
        }
      },
      "contents": {
        "twoColumnBrowseResultsRenderer": {
          "tabs": [
            {"tabRenderer": {
              "title": "Home",
              "selected": true,
              "content": {
                "richGridRenderer": {
                  "contents": [
                    {"richItemRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-video",
                      "title": {"simpleText": "Normal Home Video"},
                      "longBylineText": {"runs": [{
                        "text": "Home Channel",
                        "navigationEndpoint": {"browseEndpoint": {"browseId": "UCHOME"}}
                      }]},
                      "lengthText": {"simpleText": "12:34"},
                      "viewCountText": {"simpleText": "1,234 views"},
                      "publishedTimeText": {"simpleText": "2 days ago"},
                      "thumbnail": {"thumbnails": [{
                        "url": "https://i.ytimg.com/home-normal.jpg",
                        "width": 320,
                        "height": 180
                      }]}
                    }}}},
                    {"richItemRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-short",
                      "title": {"simpleText": "Disguised Short"},
                      "navigationEndpoint": {"reelWatchEndpoint": {"videoId": "home-short"}}
                    }}}},
                    {"richSectionRenderer": {"content": {"richShelfRenderer": {
                      "title": {"runs": [{"text": "Recommended"}]},
                      "contents": [
                        {"richItemRenderer": {"content": {"channelRenderer": {
                          "channelId": "UCHANNEL",
                          "title": {"simpleText": "Channel Card"},
                          "subscriberCountText": {"simpleText": "12.3K subscribers"},
                          "videoCountText": {"simpleText": "99 videos"},
                          "thumbnail": {"thumbnails": [{"url": "https://yt3.ggpht.com/home-channel"}]}
                        }}}},
                        {"richItemRenderer": {"content": {"playlistRenderer": {
                          "playlistId": "PLHOME",
                          "title": {"simpleText": "Playlist Card"},
                          "shortBylineText": {"runs": [{"text": "Playlist Owner"}]},
                          "videoCountText": {"simpleText": "42 videos"},
                          "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/home-playlist"}]}
                        }}}}
                      ]
                    }}}},
                    {"adSlotRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-ad",
                      "title": {"simpleText": "Sponsored escape attempt"}
                    }}}},
                    {"productRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-product",
                      "title": {"simpleText": "Shopping escape attempt"}
                    }}}},
                    {"mealbarPromoRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-premium-promo",
                      "title": {"simpleText": "Premium escape attempt"}
                    }}}},
                    {"totallyUnknownRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-unknown-wrapper",
                      "title": {"simpleText": "Unsupported escape attempt"}
                    }}}},
                    {"richSectionRenderer": {"content": {"reelShelfRenderer": {
                      "title": {"simpleText": "Shorts"},
                      "items": [{"videoRenderer": {
                        "videoId": "shorts-shelf-escape",
                        "title": {"simpleText": "Never surface from Shorts shelf"}
                      }}]
                    }}}},
                    {"richItemRenderer": {"content": {"lockupViewModel": {
                      "contentType": "LOCKUP_CONTENT_TYPE_VIDEO",
                      "contentId": "modern-home-video",
                      "contentImage": {"thumbnailViewModel": {"image": {"sources": [{
                        "url": "https://i.ytimg.com/modern-home",
                        "width": 640,
                        "height": 360
                      }]}}},
                      "metadata": {"lockupMetadataViewModel": {
                        "title": {"content": "Modern Home Video"},
                        "metadata": {"contentMetadataViewModel": {"metadataRows": [{
                          "metadataParts": [{"text": {"content": "Modern Channel"}}]
                        }]}}
                      }},
                      "rendererContext": {"commandContext": {"onTap": {"innertubeCommand": {
                        "watchEndpoint": {"videoId": "modern-home-video"}
                      }}}}
                    }}}},
                    {"richItemRenderer": {"content": {"videoRenderer": {
                      "videoId": "home-video",
                      "title": {"simpleText": "Duplicate should dedupe"}
                    }}}},
                    {"continuationItemRenderer": {
                      "continuationEndpoint": {"continuationCommand": {"token": "HOME-NEXT"}}
                    }}
                  ]
                }
              }
            }},
            {"tabRenderer": {
              "title": "Not selected",
              "selected": false,
              "content": {"richGridRenderer": {"contents": [
                {"richItemRenderer": {"content": {"videoRenderer": {
                  "videoId": "unselected-tab-video",
                  "title": {"simpleText": "Must remain out of scope"}
                }}}}
              ]}}
            }}
          ]
        }
      }
    })JSON";
}

}  // namespace

int main() {
    using ttnx::core::BrowseResultKind;
    using ttnx::youtube::parse_scoped_home_response;

    const auto parsed = parse_scoped_home_response(home_fixture());
    expect(parsed.page.has_value(), "recognized selected Home tab parses");
    expect(parsed.diagnostics.find("twoColumn=1") != std::string::npos,
           "Home diagnostics identify two-column browse layout");
    expect(parsed.diagnostics.find("tabs=2") != std::string::npos,
           "Home diagnostics count browse tabs");
    expect(parsed.diagnostics.find("selected=1") != std::string::npos,
           "Home diagnostics count selected tabs");
    expect(parsed.diagnostics.find("richGridRenderer:") != std::string::npos,
           "Home diagnostics report richGridRenderer structurally");
    expect(parsed.diagnostics.find("richItemRenderer:") != std::string::npos,
           "Home diagnostics report richItemRenderer structurally");
    expect(parsed.diagnostics.find("reelShelfRenderer:") != std::string::npos,
           "Home diagnostics report blocked reel structural family");
    expect(parsed.diagnostics.find("adSlotRenderer:") != std::string::npos,
           "Home diagnostics report blocked ad structural family");
    expect(parsed.diagnostics.find("productRenderer:") != std::string::npos,
           "Home diagnostics report blocked shopping structural family");
    expect(parsed.diagnostics.find("totallyUnknownRenderer") != std::string::npos,
           "Home diagnostics report bounded opaque family names");
    expect(parsed.diagnostics.find("home-video") == std::string::npos,
           "Home diagnostics never expose video IDs");
    expect(parsed.diagnostics.find("Normal Home Video") == std::string::npos,
           "Home diagnostics never expose video titles");
    expect(parsed.diagnostics.find("HOME-NEXT") == std::string::npos,
           "Home diagnostics never expose continuation tokens");
    expect(parsed.diagnostics.find("i.ytimg.com") == std::string::npos,
           "Home diagnostics never expose thumbnail URLs");
    if (parsed.page) {
        const auto& page = *parsed.page;
        expect(page.results.size() == 4,
               "Home exposes only deduped normal video/channel/playlist/modern video results");
        expect(page.continuation == "HOME-NEXT", "Home continuation token is preserved opaquely");
        expect(has_section(page, "Recommended"), "ordinary Home shelf title is preserved for later UI grouping");

        const auto* video = find_id(page, "home-video");
        expect(video != nullptr, "normal Home video survives");
        if (video) {
            expect(video->kind == BrowseResultKind::Video, "Home video normalized as Video");
            expect(video->channel_name == "Home Channel", "Home video channel metadata extracted");
            expect(video->channel_id == "UCHOME", "Home video channel ID extracted");
            expect(video->duration_text == "12:34", "Home video duration retained");
            expect(video->view_count && *video->view_count == 1234, "Home exact view count parsed");
            expect(video->published_text == "2 days ago", "Home upload age text retained");
            expect(!video->thumbnails.empty(), "Home thumbnail candidates normalize without fetching");
        }

        const auto* channel = find_id(page, "UCHANNEL");
        expect(channel && channel->kind == BrowseResultKind::Channel,
               "Home channel card normalized");
        if (channel) {
            expect(!channel->subscriber_count.has_value(),
                   "abbreviated subscriber count is not fabricated into an exact number");
            expect(channel->subscriber_count_text == "12.3K subscribers",
                   "unparsed subscriber display text is preserved");
        }

        const auto* playlist = find_id(page, "PLHOME");
        expect(playlist && playlist->kind == BrowseResultKind::Playlist,
               "Home playlist card normalized");
        expect(find_id(page, "modern-home-video") != nullptr,
               "modern lockup Home video is supported");

        expect(find_id(page, "home-short") == nullptr,
               "video-shaped reel endpoint cannot bypass Home Shorts firewall");
        expect(find_id(page, "shorts-shelf-escape") == nullptr,
               "Shorts shelf is opaque and contributes zero normal videos");
        expect(find_id(page, "home-ad") == nullptr,
               "ad subtree is opaque and contributes zero videos");
        expect(find_id(page, "home-product") == nullptr,
               "shopping subtree is opaque and contributes zero videos");
        expect(find_id(page, "home-premium-promo") == nullptr,
               "Premium/promo wrapper is opaque and contributes zero videos");
        expect(find_id(page, "home-unknown-wrapper") == nullptr,
               "unsupported Home renderer family is opaque and contributes zero videos");
        expect(find_id(page, "hostile-topbar-video") == nullptr,
               "renderer-shaped topbar metadata remains outside Home scope");
        expect(find_id(page, "unselected-tab-video") == nullptr,
               "unselected browse tab remains outside Home scope");
    }

    const std::string continuation = R"JSON({
      "onResponseReceivedActions": [{
        "appendContinuationItemsAction": {
          "continuationItems": [
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "home-page-two",
              "title": {"simpleText": "Home Page Two"}
            }}}},
            {"reelShelfRenderer": {"items": [{"videoRenderer": {
              "videoId": "page-two-short-escape",
              "title": {"simpleText": "Never"}
            }}]}},
            {"continuationItemViewModel": {
              "continuationCommand": {"token": "HOME-PAGE-THREE"}
            }}
          ]
        }
      }],
      "menu": {"videoRenderer": {
        "videoId": "continuation-menu-escape",
        "title": {"simpleText": "Out of scope"}
      }}
    })JSON";
    const auto next = parse_scoped_home_response(continuation);
    expect(next.page.has_value(), "Home continuation action scope parses for future continuation work");
    if (next.page) {
        expect(find_id(*next.page, "home-page-two") != nullptr,
               "Home continuation normal result survives");
        expect(find_id(*next.page, "page-two-short-escape") == nullptr,
               "Home continuation Shorts shelf remains blocked");
        expect(find_id(*next.page, "continuation-menu-escape") == nullptr,
               "Home continuation sibling command metadata remains out of scope");
        expect(next.page->continuation == "HOME-PAGE-THREE",
               "Home continuation token remains opaque");
    }

    const auto opaque_selected = parse_scoped_home_response(R"JSON({
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": true, "content": {"richGridRenderer": {"contents": [
{"mysteryHomeRenderer": {"contents": [
  {"videoRenderer": {"videoId": "PRIVATE-OPAQUE-ID", "title": {"simpleText": "Private Opaque Title"}}}
]}}
        ]}}}}
      ]}}
    })JSON");
    expect(opaque_selected.page.has_value(), "opaque selected Home wrapper yields an empty normalized page, not unsafe recursion");
    if (opaque_selected.page) {
        expect(opaque_selected.page->results.empty(), "opaque Home wrapper contributes zero results");
    }
    expect(opaque_selected.diagnostics.find("mysteryHomeRenderer") != std::string::npos,
 "opaque Home family name is structurally reported");
    expect(opaque_selected.diagnostics.find("PRIVATE-OPAQUE-ID") == std::string::npos,
 "opaque Home diagnostics do not expose nested IDs");
    expect(opaque_selected.diagnostics.find("Private Opaque Title") == std::string::npos,
 "opaque Home diagnostics do not expose nested titles");
    expect(opaque_selected.diagnostics.find("videoRenderer") == std::string::npos,
 "diagnostic scanner does not descend through an unreviewed wrapper");

    const auto single_column = parse_scoped_home_response(R"JSON({
      "contents": {"singleColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": true, "content": {"sectionListRenderer": {"contents": []}}}}
      ]}}
    })JSON");
    expect(single_column.page.has_value(), "single-column empty Home scope is recognized");
    expect(single_column.diagnostics.find("singleColumn=1") != std::string::npos,
 "Home diagnostics identify single-column browse layout");

    const auto unsupported = parse_scoped_home_response(R"JSON({
      "topbar": {"videoRenderer": {"videoId": "x", "title": {"simpleText": "x"}}}
    })JSON");
    expect(!unsupported.page.has_value(), "renderer-shaped data outside recognized Home scope is rejected");

    const auto malformed = parse_scoped_home_response("{broken");
    expect(!malformed.page.has_value(), "malformed Home JSON is rejected");

    std::string oversized(8 * 1024 * 1024 + 1, ' ');
    const auto too_large = parse_scoped_home_response(oversized);
    expect(!too_large.page.has_value(), "oversized Home response is rejected before traversal");

    if (failures == 0) {
        std::cout << "All scoped Home response tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
