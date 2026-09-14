#include "tizentube_nx/youtube/channel_response.hpp"

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

constexpr const char* kChannelId = "UCabcdefghijklmnopqrstuv";

const ttnx::core::BrowseResult* find_id(const ttnx::core::ChannelPage& page, const std::string& id) {
    for (const auto& result : page.results) if (result.id == id) return &result;
    return nullptr;
}

std::string normal_fixture() {
    return R"JSON({
      "topbar": {"desktopTopbarRenderer": {"videoRenderer": {
        "videoId": "topbarxxxxx", "title": {"simpleText": "Never"}
      }}},
      "metadata": {"channelMetadataRenderer": {
        "title": "Fixture Channel",
        "externalId": "UCabcdefghijklmnopqrstuv",
        "description": "A bounded channel description.",
        "vanityChannelUrl": "https://www.youtube.com/@fixturechannel"
      }},
      "header": {"c4TabbedHeaderRenderer": {
        "channelId": "UCabcdefghijklmnopqrstuv",
        "title": "Fixture Channel",
        "channelHandleText": {"simpleText": "@fixturechannel"},
        "subscriberCountText": {"simpleText": "12.3K subscribers"}
      }},
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {
          "selected": false,
          "content": {"richGridRenderer": {"contents": [
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "unselectedx", "title": {"simpleText": "Unselected fake"}
            }}}}
          ]}}
        }},
        {"tabRenderer": {
          "selected": true,
          "content": {"richGridRenderer": {"contents": [
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "abcdefghijk",
              "title": {"simpleText": "Normal Channel Video"},
              "longBylineText": {"runs": [{"text": "Fixture Channel", "navigationEndpoint": {"browseEndpoint": {"browseId": "UCabcdefghijklmnopqrstuv"}}}]},
              "lengthText": {"simpleText": "10:20"},
              "viewCountText": {"simpleText": "1,234 views"},
              "publishedTimeText": {"simpleText": "3 days ago"}
            }}}},
            {"richItemRenderer": {"content": {"playlistRenderer": {
              "playlistId": "PLCHANNEL123",
              "title": {"simpleText": "Normal Playlist"},
              "shortBylineText": {"runs": [{"text": "Fixture Channel"}]},
              "videoCountText": {"simpleText": "12 videos"}
            }}}},
            {"richItemRenderer": {"content": {"channelRenderer": {
              "channelId": "UC1234567890123456789012",
              "title": {"simpleText": "Related Channel"}
            }}}},
            {"richItemRenderer": {"content": {"lockupViewModel": {
              "contentType": "LOCKUP_CONTENT_TYPE_VIDEO",
              "contentId": "lmnopqrstuv",
              "metadata": {"lockupMetadataViewModel": {"title": {"content": "Modern Channel Video"}}},
              "rendererContext": {"commandContext": {"onTap": {"innertubeCommand": {"watchEndpoint": {"videoId": "lmnopqrstuv"}}}}}
            }}}},
            {"richSectionRenderer": {"content": {"reelShelfRenderer": {
              "items": [{"videoRenderer": {"videoId": "shortescape1", "title": {"simpleText": "Short escape"}}}]
            }}}},
            {"adSlotRenderer": {"content": {"videoRenderer": {
              "videoId": "adescape123", "title": {"simpleText": "Ad escape"}
            }}}},
            {"productRenderer": {"content": {"videoRenderer": {
              "videoId": "shopescape1", "title": {"simpleText": "Shop escape"}
            }}}},
            {"mealbarPromoRenderer": {"content": {"videoRenderer": {
              "videoId": "promoescape", "title": {"simpleText": "Promo escape"}
            }}}},
            {"mysteryChannelRenderer": {"content": {"videoRenderer": {
              "videoId": "opaquevideo1", "title": {"simpleText": "Opaque escape"}
            }}}},
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "shortlookal",
              "title": {"simpleText": "Disguised Short"},
              "navigationEndpoint": {"reelWatchEndpoint": {"videoId": "shortlookal"}}
            }}}},
            {"richItemRenderer": {"content": {"videoRenderer": {
              "videoId": "abcdefghijk", "title": {"simpleText": "Duplicate"}
            }}}},
            {"continuationItemRenderer": {"continuationEndpoint": {"continuationCommand": {"token": "CHANNEL-NEXT"}}}}
          ]}}
        }}
      ]}}
    })JSON";
}

}  // namespace

int main() {
    using ttnx::core::BrowseResultKind;
    using ttnx::youtube::parse_scoped_channel_response;

    const auto parsed = parse_scoped_channel_response(normal_fixture(), kChannelId);
    expect(parsed.page.has_value(), "selected Channel tab parses");
    expect(parsed.diagnostics.find("Channel diag:") != std::string::npos,
           "Channel diagnostics are clearly scoped");
    expect(parsed.diagnostics.find("selected=1") != std::string::npos,
           "Channel diagnostics count selected tabs");
    expect(parsed.diagnostics.find("mysteryChannelRenderer") != std::string::npos,
           "Channel diagnostics report bounded opaque family names");
    expect(parsed.diagnostics.find("CHANNEL-NEXT") == std::string::npos,
           "Channel diagnostics never expose continuation token values");
    expect(parsed.diagnostics.find("Normal Channel Video") == std::string::npos,
           "Channel diagnostics never expose titles");

    if (parsed.page) {
        const auto& page = *parsed.page;
        expect(page.identity.browse_id == kChannelId, "Channel identity anchored to requested browse id");
        expect(page.metadata.title == "Fixture Channel", "Channel metadata title parsed");
        expect(page.metadata.handle == "@fixturechannel", "Channel handle parsed structurally");
        expect(page.metadata.subscriber_text == "12.3K subscribers", "subscriber display text preserved without numeric fabrication");
        expect(page.metadata.description == "A bounded channel description.", "Channel description parsed from reviewed metadata renderer");
        expect(page.metadata.canonical_url == "https://www.youtube.com/channel/UCabcdefghijklmnopqrstuv",
               "Channel canonical URL is generated locally from stable ID");
        expect(page.results.size() == 4, "normal video/playlist/channel/lockup survive with duplicate removed");
        expect(page.continuation == "CHANNEL-NEXT", "Channel continuation seam retained but not exposed in UI");
        expect(find_id(page, "abcdefghijk") != nullptr, "normal Channel video survives");
        const auto* playlist = find_id(page, "PLCHANNEL123");
        expect(playlist && playlist->kind == BrowseResultKind::Playlist,
               "normal Channel playlist survives");
        const auto* related = find_id(page, "UC1234567890123456789012");
        expect(related && related->kind == BrowseResultKind::Channel,
               "legitimate Channel result survives reviewed scope");
        expect(find_id(page, "lmnopqrstuv") != nullptr, "modern lockup normal video survives");
        expect(find_id(page, "unselectedx") == nullptr, "unselected Channel tab remains isolated");
        expect(find_id(page, "shortescape1") == nullptr, "Shorts shelf remains opaque");
        expect(find_id(page, "adescape123") == nullptr, "ad subtree remains opaque");
        expect(find_id(page, "shopescape1") == nullptr, "shopping subtree remains opaque");
        expect(find_id(page, "promoescape") == nullptr, "Premium/promo subtree remains opaque");
        expect(find_id(page, "opaquevideo1") == nullptr, "unknown wrapper remains opaque");
        expect(find_id(page, "shortlookal") == nullptr, "disguised reel endpoint fails closed");
        expect(find_id(page, "topbarxxxxx") == nullptr, "topbar renderer-shaped content remains out of scope");
    }

    const auto empty = parse_scoped_channel_response(R"JSON({
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": true, "content": {"richGridRenderer": {"contents": []}}}}
      ]}}
    })JSON", kChannelId);
    expect(empty.page.has_value(), "valid empty selected Channel tab parses as empty page");
    if (empty.page) expect(empty.page->results.empty(), "valid empty Channel has no invented content");

    const auto unknown_only = parse_scoped_channel_response(R"JSON({
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": true, "content": {"richGridRenderer": {"contents": [
          {"unknownOnlyRenderer": {"content": {"videoRenderer": {"videoId": "abcdefghijk", "title": {"simpleText": "Do not recurse"}}}}}
        ]}}}}
      ]}}
    })JSON", kChannelId);
    expect(!unknown_only.page.has_value(), "unknown-only selected Channel structure is classified unsupported");
    expect(unknown_only.diagnostics.find("unknownOnlyRenderer") != std::string::npos,
           "unsupported Channel returns bounded family diagnostic");
    expect(unknown_only.diagnostics.find("abcdefghijk") == std::string::npos,
           "unsupported Channel diagnostic does not leak nested ID");

    const auto no_selected = parse_scoped_channel_response(R"JSON({
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": false, "content": {"richGridRenderer": {"contents": []}}}}
      ]}}
    })JSON", kChannelId);
    expect(!no_selected.page.has_value(), "Channel parser does not pretend an unselected tab is selected");

    const auto two_selected = parse_scoped_channel_response(R"JSON({
      "contents": {"twoColumnBrowseResultsRenderer": {"tabs": [
        {"tabRenderer": {"selected": true, "content": {"richGridRenderer": {"contents": []}}}},
        {"tabRenderer": {"selected": true, "content": {"richGridRenderer": {"contents": []}}}}
      ]}}
    })JSON", kChannelId);
    expect(!two_selected.page.has_value(), "multiple selected Channel tabs fail closed");

    const auto malformed = parse_scoped_channel_response("{broken", kChannelId);
    expect(!malformed.page.has_value(), "malformed Channel response rejected separately");

    const auto wrong_identity = parse_scoped_channel_response(normal_fixture(), "not-a-channel");
    expect(!wrong_identity.page.has_value(), "invalid requested Channel identity rejected before parsing");

    if (failures == 0) {
        std::cout << "All Channel response tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
