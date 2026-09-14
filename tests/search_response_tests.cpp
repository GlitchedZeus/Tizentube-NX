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
    using ttnx::youtube::parse_scoped_search_response;

    const std::string hostile_first_page = R"JSON({
      "topbar": {
        "videoRenderer": {
          "videoId": "FAKE_TOPBAR_VIDEO",
          "title": {"runs": [{"text": "THIS MUST NEVER BECOME A SEARCH RESULT"}]}
        }
      },
      "header": {
        "lockupViewModel": {
          "contentType": "LOCKUP_CONTENT_TYPE_VIDEO",
          "contentId": "FAKE_HEADER_LOCKUP",
          "metadata": {"lockupMetadataViewModel": {"title": {"content": "Fake Header"}}}
        }
      },
      "sidebar": {
        "menuRenderer": {
          "items": [{"playlistRenderer": {
            "playlistId": "FAKE_SIDEBAR_PLAYLIST",
            "title": {"simpleText": "Fake Sidebar Playlist"}
          }}]
        }
      },
      "metadata": {
        "channelRenderer": {
          "channelId": "FAKE_METADATA_CHANNEL",
          "title": {"simpleText": "Fake Metadata Channel"}
        }
      },
      "contents": {
        "twoColumnSearchResultsRenderer": {
          "primaryContents": {
            "sectionListRenderer": {
              "contents": [{
                "itemSectionRenderer": {
                  "contents": [
                    {"videoRenderer": {
                      "videoId": "LEGIT_VIDEO",
                      "title": {"simpleText": "Legit Video"}
                    }},
                    {"lockupViewModel": {
                      "contentType": "LOCKUP_CONTENT_TYPE_VIDEO",
                      "contentId": "LEGIT_LOCKUP",
                      "metadata": {"lockupMetadataViewModel": {"title": {"content": "Legit Lockup"}}},
                      "rendererContext": {"commandContext": {"onTap": {"innertubeCommand": {
                        "watchEndpoint": {"videoId": "LEGIT_LOCKUP"}
                      }}}}
                    }},
                    {"channelRenderer": {
                      "channelId": "LEGIT_CHANNEL",
                      "title": {"simpleText": "Legit Channel"}
                    }},
                    {"playlistRenderer": {
                      "playlistId": "LEGIT_PLAYLIST",
                      "title": {"simpleText": "Legit Playlist"}
                    }},
                    {"videoRenderer": {
                      "videoId": "BLOCKED_REEL_VIDEO",
                      "title": {"simpleText": "Claims normal"},
                      "navigationEndpoint": {"reelWatchEndpoint": {"videoId": "BLOCKED_REEL_VIDEO"}}
                    }},
                    {"adSlotRenderer": {
                      "content": {"videoRenderer": {
                        "videoId": "BLOCKED_AD_VIDEO",
                        "title": {"simpleText": "Nested ad"}
                      }}
                    }},
                    {"productRenderer": {
                      "content": {"videoRenderer": {
                        "videoId": "BLOCKED_SHOPPING_VIDEO",
                        "title": {"simpleText": "Nested shopping"}
                      }}
                    }},
                    {"mysteryRenderer": {
                      "contentId": "BLOCKED_UNKNOWN",
                      "title": {"simpleText": "Unknown renderer"}
                    }}
                  ]
                }
              }, {
                "continuationItemRenderer": {
                  "continuationEndpoint": {
                    "continuationCommand": {"token": "LEGACY-NEXT"}
                  }
                }
              }]
            }
          }
        }
      }
    })JSON";

    const auto first = parse_scoped_search_response(hostile_first_page);
    expect(first.page.has_value(), "scoped first-page Search parses");
    if (first.page) {
        const auto& page = *first.page;
        expect(page.items.size() == 4,
               "only legitimate video, lockup, channel and playlist survive scoped Search");
        expect(find_id(page, "LEGIT_VIDEO") != nullptr, "legacy videoRenderer survives inside Search scope");
        const auto* lockup = find_id(page, "LEGIT_LOCKUP");
        expect(lockup != nullptr, "modern lockupViewModel survives inside Search scope");
        if (lockup) expect(lockup->item.kind == ContentKind::Video, "modern lockup maps to video");
        expect(find_id(page, "LEGIT_CHANNEL") != nullptr, "channelRenderer survives inside Search scope");
        expect(find_id(page, "LEGIT_PLAYLIST") != nullptr, "playlistRenderer survives inside Search scope");
        expect(page.continuation == "LEGACY-NEXT", "legacy continuation token survives scoped Search");

        expect(find_id(page, "FAKE_TOPBAR_VIDEO") == nullptr, "topbar renderer cannot escape Search scope");
        expect(find_id(page, "FAKE_HEADER_LOCKUP") == nullptr, "header lockup cannot escape Search scope");
        expect(find_id(page, "FAKE_SIDEBAR_PLAYLIST") == nullptr, "sidebar renderer cannot escape Search scope");
        expect(find_id(page, "FAKE_METADATA_CHANNEL") == nullptr, "metadata renderer cannot escape Search scope");
        expect(find_id(page, "BLOCKED_REEL_VIDEO") == nullptr, "reel endpoint remains blocked inside Search scope");
        expect(find_id(page, "BLOCKED_AD_VIDEO") == nullptr, "ad subtree remains opaque inside Search scope");
        expect(find_id(page, "BLOCKED_SHOPPING_VIDEO") == nullptr,
               "shopping subtree remains opaque inside Search scope");
        expect(find_id(page, "BLOCKED_UNKNOWN") == nullptr, "unknown renderer still fails closed");
    }

    const std::string hostile_continuation = R"JSON({
      "topbar": {
        "videoRenderer": {
          "videoId": "FAKE_CONTINUATION_TOPBAR",
          "title": {"simpleText": "Never return this"}
        }
      },
      "onResponseReceivedCommands": [
        {"appendContinuationItemsAction": {
          "continuationItems": [
            {"videoRenderer": {
              "videoId": "PAGE_TWO_VIDEO",
              "title": {"simpleText": "Page Two"}
            }},
            {"lockupViewModel": {
              "contentType": "LOCKUP_CONTENT_TYPE_SHORT",
              "contentId": "PAGE_TWO_SHORT",
              "metadata": {"lockupMetadataViewModel": {"title": {"content": "Never surface"}}}
            }},
            {"continuationItemView": {
              "trigger": "CONTINUATION_TRIGGER_ON_ITEM_SHOWN",
              "continuationCommand": {"token": "MODERN-NEXT"}
            }}
          ]
        }},
        {"menuCommand": {
          "videoRenderer": {
            "videoId": "FAKE_COMMAND_VIDEO",
            "title": {"simpleText": "Sibling command renderer"}
          }
        }},
        {"unrelatedAction": {
          "appendContinuationItemsAction": {
            "continuationItems": [{"videoRenderer": {
              "videoId": "FAKE_NESTED_APPEND_VIDEO",
              "title": {"simpleText": "Nested fake append"}
            }}]
          }
        }}
      ],
      "onResponseReceivedActions": [
        {"reloadContinuationItemsCommand": {
          "continuationItems": [{"channelRenderer": {
            "channelId": "PAGE_TWO_CHANNEL",
            "title": {"simpleText": "Page Two Channel"}
          }}]
        }}
      ],
      "metadata": {
        "playlistRenderer": {
          "playlistId": "FAKE_CONTINUATION_METADATA",
          "title": {"simpleText": "Metadata fake"}
        }
      }
    })JSON";

    const auto continuation = parse_scoped_search_response(hostile_continuation);
    expect(continuation.page.has_value(), "scoped continuation response parses");
    if (continuation.page) {
        const auto& page = *continuation.page;
        expect(page.items.size() == 2, "only direct continuation payload results survive");
        expect(find_id(page, "PAGE_TWO_VIDEO") != nullptr, "continuation video survives");
        expect(find_id(page, "PAGE_TWO_CHANNEL") != nullptr, "reload continuation channel survives");
        expect(page.continuation == "MODERN-NEXT", "modern continuation-item view token survives");
        expect(find_id(page, "PAGE_TWO_SHORT") == nullptr, "continuation Shorts remain blocked");
        expect(find_id(page, "FAKE_CONTINUATION_TOPBAR") == nullptr,
               "continuation topbar renderer cannot escape scope");
        expect(find_id(page, "FAKE_COMMAND_VIDEO") == nullptr,
               "unrelated continuation command renderer cannot escape scope");
        expect(find_id(page, "FAKE_NESTED_APPEND_VIDEO") == nullptr,
               "nested unrecognized append action cannot reopen recursive scanning");
        expect(find_id(page, "FAKE_CONTINUATION_METADATA") == nullptr,
               "continuation metadata renderer cannot escape scope");
    }

    const auto only_out_of_scope = parse_scoped_search_response(R"JSON({
      "topbar": {"videoRenderer": {
        "videoId": "ONLY_FAKE",
        "title": {"simpleText": "No actual Search payload"}
      }}
    })JSON");
    expect(!only_out_of_scope.page.has_value(),
           "renderer-looking JSON without a recognized Search payload is rejected");

    const auto malformed = parse_scoped_search_response("{\"contents\":[}");
    expect(!malformed.page.has_value(), "malformed scoped Search JSON rejected");
    expect(!malformed.error.empty(), "malformed scoped Search returns an error");

    std::string oversized(8 * 1024 * 1024 + 1, ' ');
    const auto too_large = parse_scoped_search_response(oversized);
    expect(!too_large.page.has_value(), "oversized scoped Search response rejected before parsing");

    if (failures == 0) {
        std::cout << "All scoped Search response tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
