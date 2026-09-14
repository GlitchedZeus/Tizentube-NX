#include "tizentube_nx/youtube/browse_response.hpp"

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

const ttnx::core::RendererRecord* find_id(
    const ttnx::core::BrowsePage& page,
    const std::string& id) {
    for (const auto& record : page.items) {
        if (record.item.id == id) return &record;
    }
    return nullptr;
}

}  // namespace

int main() {
    using ttnx::core::ContentKind;
    using ttnx::youtube::parse_search_response;

    const std::string legacy = R"JSON({
      "contents": {
        "twoColumnSearchResultsRenderer": {
          "primaryContents": {
            "sectionListRenderer": {
              "contents": [{
                "itemSectionRenderer": {
                  "contents": [
                    {"videoRenderer": {
                      "videoId": "normal-video",
                      "title": {"runs": [{"text": "Normal "}, {"text": "Video"}]},
                      "longBylineText": {"runs": [{"text": "Normal Channel"}]},
                      "thumbnail": {"thumbnails": [
                        {"url": "https://i.ytimg.com/low.jpg"},
                        {"url": "https://i.ytimg.com/high.jpg"}
                      ]},
                      "lengthText": {"simpleText": "12:34"}
                    }},
                    {"videoRenderer": {
                      "videoId": "disguised-short",
                      "title": {"simpleText": "Looks normal"},
                      "navigationEndpoint": {"reelWatchEndpoint": {"videoId": "disguised-short"}}
                    }},
                    {"adSlotRenderer": {
                      "content": {"videoRenderer": {
                        "videoId": "ad-escape-attempt",
                        "title": {"simpleText": "Sponsored but nested like a video"}
                      }}
                    }},
                    {"reelShelfRenderer": {
                      "items": [{"videoRenderer": {
                        "videoId": "short-shelf-escape-attempt",
                        "title": {"simpleText": "Nested in a Shorts shelf"}
                      }}]
                    }},
                    {"channelRenderer": {
                      "channelId": "UC123",
                      "title": {"simpleText": "Channel Result"},
                      "thumbnail": {"thumbnails": [{"url": "https://yt3.ggpht.com/channel"}]}
                    }},
                    {"playlistRenderer": {
                      "playlistId": "PL123",
                      "title": {"simpleText": "Playlist Result"},
                      "shortBylineText": {"runs": [{"text": "Playlist Owner"}]},
                      "thumbnail": {"thumbnails": [{"url": "https://i.ytimg.com/playlist"}]}
                    }},
                    {"mysteryRenderer": {
                      "contentId": "unknown-should-not-surface",
                      "title": {"simpleText": "Unknown"}
                    }}
                  ]
                }
              }, {
                "continuationItemRenderer": {
                  "trigger": "CONTINUATION_TRIGGER_ON_ITEM_SHOWN",
                  "continuationEndpoint": {
                    "continuationCommand": {"token": "CONT-LEGACY"}
                  }
                }
              }]
            }
          }
        }
      }
    })JSON";

    const auto parsed_legacy = parse_search_response(legacy);
    expect(parsed_legacy.page.has_value(), "legacy search response parses");
    if (parsed_legacy.page) {
        const auto& page = *parsed_legacy.page;
        expect(page.items.size() == 3, "only normal video, channel and playlist survive firewall");
        expect(page.continuation == "CONT-LEGACY", "legacy continuation token preserved opaquely");

        const auto* video = find_id(page, "normal-video");
        expect(video != nullptr, "normal legacy video survives");
        if (video) {
            expect(video->item.kind == ContentKind::Video, "legacy video kind mapped");
            expect(video->item.title == "Normal Video", "legacy title runs concatenated");
            expect(video->channel_title == "Normal Channel", "legacy channel title extracted");
            expect(video->thumbnail_url == "https://i.ytimg.com/high.jpg",
                   "highest/last legacy thumbnail selected");
            expect(video->duration_text == "12:34", "legacy duration extracted");
        }

        const auto* channel = find_id(page, "UC123");
        expect(channel && channel->item.kind == ContentKind::Channel,
               "channel renderer mapped to channel");

        const auto* playlist = find_id(page, "PL123");
        expect(playlist && playlist->item.kind == ContentKind::Playlist,
               "playlist renderer mapped to playlist");
        if (playlist) {
            expect(playlist->channel_title == "Playlist Owner", "playlist owner extracted");
        }

        expect(find_id(page, "disguised-short") == nullptr,
               "videoRenderer with reel endpoint cannot bypass Shorts firewall");
        expect(find_id(page, "ad-escape-attempt") == nullptr,
               "video nested inside ad subtree cannot escape");
        expect(find_id(page, "short-shelf-escape-attempt") == nullptr,
               "video nested inside reel shelf cannot escape");
        expect(find_id(page, "unknown-should-not-surface") == nullptr,
               "unknown leaf renderer is not guessed into a card");
    }

    const std::string modern = R"JSON({
      "onResponseReceivedCommands": [{
        "appendContinuationItemsAction": {
          "continuationItems": [
            {"lockupViewModel": {
              "contentType": "LOCKUP_CONTENT_TYPE_VIDEO",
              "contentId": "modern-video",
              "contentImage": {
                "thumbnailViewModel": {
                  "image": {"sources": [
                    {"url": "https://i.ytimg.com/modern-low"},
                    {"url": "https://i.ytimg.com/modern-high"}
                  ]}
                }
              },
              "metadata": {
                "lockupMetadataViewModel": {
                  "title": {"content": "Modern Video"},
                  "metadata": {
                    "contentMetadataViewModel": {
                      "metadataRows": [{
                        "metadataParts": [{"text": {"content": "Modern Channel"}}]
                      }]
                    }
                  }
                }
              },
              "rendererContext": {
                "commandContext": {
                  "onTap": {"innertubeCommand": {"watchEndpoint": {"videoId": "modern-video"}}}
                }
              }
            }},
            {"lockupViewModel": {
              "contentType": "LOCKUP_CONTENT_TYPE_SHORT",
              "contentId": "modern-short",
              "metadata": {"lockupMetadataViewModel": {"title": {"content": "Modern Short"}}}
            }},
            {"lockupViewModel": {
              "contentType": "LOCKUP_CONTENT_TYPE_VIDEO",
              "contentId": "lying-modern-short",
              "metadata": {"lockupMetadataViewModel": {"title": {"content": "Claims video"}}},
              "rendererContext": {
                "commandContext": {
                  "onTap": {"innertubeCommand": {"reelWatchEndpoint": {"videoId": "lying-modern-short"}}}
                }
              }
            }},
            {"lockupViewModel": {
              "contentType": "LOCKUP_CONTENT_TYPE_PODCAST",
              "contentId": "unsupported-lockup",
              "metadata": {"lockupMetadataViewModel": {"title": {"content": "Unsupported"}}}
            }},
            {"continuationItemViewModel": {
              "trigger": "CONTINUATION_TRIGGER_ON_ITEM_SHOWN",
              "continuationCommand": {"token": "CONT-MODERN"}
            }}
          ]
        }
      }]
    })JSON";

    const auto parsed_modern = parse_search_response(modern);
    expect(parsed_modern.page.has_value(), "modern lockup response parses");
    if (parsed_modern.page) {
        const auto& page = *parsed_modern.page;
        expect(page.items.size() == 1, "only supported modern video survives");
        expect(page.continuation == "CONT-MODERN", "modern continuation view token preserved");
        const auto* modern_video = find_id(page, "modern-video");
        expect(modern_video != nullptr, "modern lockup video survives");
        if (modern_video) {
            expect(modern_video->item.kind == ContentKind::Video, "modern lockup video kind mapped");
            expect(modern_video->item.title == "Modern Video", "modern lockup title extracted");
            expect(modern_video->channel_title == "Modern Channel",
                   "modern metadata row channel extracted");
            expect(modern_video->thumbnail_url == "https://i.ytimg.com/modern-high",
                   "modern thumbnail sources parsed");
        }
        expect(find_id(page, "modern-short") == nullptr, "modern SHORT contentType dropped");
        expect(find_id(page, "lying-modern-short") == nullptr,
               "modern VIDEO contentType with reel endpoint still dropped");
        expect(find_id(page, "unsupported-lockup") == nullptr,
               "unknown modern contentType fails closed");
    }

    const std::string conflicting_continuations = R"JSON({
      "contents": [
        {"continuationItemRenderer": {
          "continuationEndpoint": {"continuationCommand": {"token": "ONE"}}
        }},
        {"continuationItemViewModel": {
          "continuationCommand": {"token": "TWO"}
        }}
      ]
    })JSON";
    const auto conflicting = parse_search_response(conflicting_continuations);
    expect(conflicting.page.has_value(), "ambiguous continuation response remains usable");
    if (conflicting.page) {
        expect(conflicting.page->continuation.empty(),
               "distinct continuation candidates disable pagination instead of guessing");
    }

    const auto malformed = parse_search_response("{\"contents\":[}");
    expect(!malformed.page.has_value(), "malformed JSON rejected");
    expect(!malformed.error.empty(), "malformed JSON returns an error");

    const auto empty_valid = parse_search_response("{\"contents\":[]}");
    expect(empty_valid.page.has_value(), "valid empty search response accepted");
    if (empty_valid.page) expect(empty_valid.page->items.empty(), "empty response has no cards");

    std::string oversized(8 * 1024 * 1024 + 1, ' ');
    const auto too_large = parse_search_response(oversized);
    expect(!too_large.page.has_value(), "oversized search response rejected before parsing");

    if (failures == 0) {
        std::cout << "All guest browse response tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
