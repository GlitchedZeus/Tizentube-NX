#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include "tizentube_nx/core/settings.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::optional<std::string_view> header_value(
    const std::vector<ttnx::net::HttpHeader>& headers,
    std::string_view name) {
    for (const auto& header : headers) {
        if (header.name == name) return header.value;
    }
    return std::nullopt;
}

}  // namespace

int main() {
    using namespace ttnx::core;

    expect(is_valid_video_id("IzJ7R4EnYmI"), "valid YouTube ID accepted");
    expect(!is_valid_video_id("too-short"), "invalid YouTube ID rejected");

    const auto canonical = canonical_watch_url("IzJ7R4EnYmI");
    expect(canonical && *canonical == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "canonical watch URL format");

    const auto dirty = canonicalize_youtube_video_url(
        "https://www.youtube.com/watch?v=IzJ7R4EnYmI&si=abcdef&feature=shared&utm_source=x");
    expect(dirty && *dirty == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "tracking parameters are discarded");

    const auto short_link = canonicalize_youtube_video_url("https://youtu.be/IzJ7R4EnYmI?si=abcdef");
    expect(short_link && *short_link == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "youtu.be is normalized");

    expect(is_shorts_url("https://www.youtube.com/shorts/IzJ7R4EnYmI"),
           "shorts URL recognized");
    expect(!canonicalize_youtube_video_url("https://www.youtube.com/shorts/IzJ7R4EnYmI"),
           "shorts URL rejected rather than surfaced");

    FilterPolicy relaxed;
    relaxed.hide_shorts = false; // must not override the hard invariant
    relaxed.hide_promoted = false; // ads/promotions are also non-negotiable
    relaxed.hide_shopping = false;

    expect(should_hide({ContentKind::Short, "id", "short"}, relaxed),
           "Shorts stay hidden even if a future setting is misconfigured");
    expect(should_hide({ContentKind::Video, "id", "ad", true, false}, relaxed),
           "promoted content stays hidden even if a future setting is misconfigured");
    expect(should_hide({ContentKind::Video, "id", "ad", true, false}),
           "promoted content hidden by default");
    expect(!should_hide({ContentKind::Video, "id", "normal"}),
           "normal video remains visible");

    expect(valid_guest_request({BrowseSurface::Home, "", ""}),
           "guest Home request may omit a key");
    expect(valid_guest_request({BrowseSurface::Search, "switch homebrew", ""}),
           "guest Search request requires search text");
    expect(!valid_guest_request({BrowseSurface::Search, "", ""}),
           "empty initial Search request rejected");
    expect(valid_guest_request({BrowseSurface::Search, "", "CONTINUATION"}),
           "opaque continuation can drive the next Search page");

    expect(classify_renderer("videoRenderer") == ContentKind::Video,
           "video renderer classified");
    expect(classify_renderer("channelRenderer") == ContentKind::Channel,
           "channel renderer classified");
    expect(classify_renderer("playlistRenderer") == ContentKind::Playlist,
           "playlist renderer classified");
    expect(classify_renderer("reelItemRenderer") == ContentKind::Short,
           "legacy reel renderer classified as Shorts");
    expect(classify_renderer("shortsLockupViewModel") == ContentKind::Short,
           "Shorts view model classified as Shorts");

    expect(renderer_disposition({"reelItemRenderer", {ContentKind::Video, "s", "short"}}) ==
               RendererDisposition::DropShorts,
           "renderer-name firewall drops disguised Shorts");
    expect(renderer_disposition({"promotedVideoRenderer", {ContentKind::Video, "a", "ad"}}) ==
               RendererDisposition::DropPromoted,
           "renderer-name firewall drops promoted videos");
    expect(renderer_disposition({"adSlotRenderer", {ContentKind::Unknown, "", "ad slot"}}) ==
               RendererDisposition::DropPromoted,
           "renderer-name firewall drops ad slots");

    std::vector<RendererRecord> raw_page{
        {"videoRenderer", {ContentKind::Video, "video1", "Normal video"}, "Channel", "thumb", "12:34"},
        {"reelItemRenderer", {ContentKind::Video, "short1", "Short disguised as video"}},
        {"adSlotRenderer", {ContentKind::Unknown, "", "Advertisement"}},
        {"channelRenderer", {ContentKind::Channel, "UC123", "A channel"}},
        {"shoppingShelfRenderer", {ContentKind::Unknown, "shop", "Shopping", false, true}},
        {"mysteryRenderer", {ContentKind::Unknown, "mystery", "Unknown"}},
    };
    const auto sanitized = sanitize_guest_page(std::move(raw_page), "NEXT_PAGE");
    expect(sanitized.items.size() == 2,
           "guest page exposes only supported non-Shorts non-ad renderers");
    expect(sanitized.items[0].item.id == "video1", "normal video survives renderer firewall");
    expect(sanitized.items[1].item.id == "UC123", "channel survives renderer firewall");
    expect(sanitized.has_more() && sanitized.continuation == "NEXT_PAGE",
           "pagination continuation is preserved as opaque data");

    using namespace ttnx::youtube;

    const auto bootstrap = make_session_bootstrap_request(
        "en-CA", "America/Toronto", "TizenTubeNX-Test/1.0", "Visitor12345");
    expect(bootstrap.method == ttnx::net::HttpMethod::Get,
           "session bootstrap uses GET");
    expect(bootstrap.url == "https://www.youtube.com/sw.js_data",
           "session bootstrap targets YouTube sw.js_data");
    expect(header_value(bootstrap.headers, "Referer") == "https://www.youtube.com/sw.js",
           "session bootstrap sets sw.js referer");
    expect(header_value(bootstrap.headers, "Cookie") ==
               "PREF=tz=America.Toronto;VISITOR_INFO1_LIVE=Visitor12345;",
           "session bootstrap normalizes timezone and carries visitor cookie id");

    GuestSession session;
    session.client_version = "test-client-version";
    session.client_name_id = "1";
    session.visitor_data = "test-visitor-data";
    session.language = "en-CA";
    session.region = "CA";
    session.timezone = "America/Toronto";
    session.user_agent = "TizenTubeNX-Test/1.0";

    expect(session.usable(), "complete guest session is usable");

    const auto search = make_search_request(session, "Switch \"homebrew\" \\ test\nline");
    expect(search.has_value(), "initial search request builds");
    if (search) {
        expect(search->method == ttnx::net::HttpMethod::Post,
               "search request uses POST");
        expect(search->url ==
                   "https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json",
               "search request uses current InnerTube production path without embedded key");
        expect(search->url.find("key=") == std::string::npos,
               "search URL does not embed a fixed API key");
        expect(header_value(search->headers, "X-Goog-Visitor-Id") == "test-visitor-data",
               "search request carries visitor data header");
        expect(header_value(search->headers, "X-Youtube-Client-Version") == "test-client-version",
               "search request carries client version header");
        expect(header_value(search->headers, "X-Youtube-Client-Name") == "1",
               "search request carries optional numeric client name header");
        expect(search->body.find("\\\"homebrew\\\"") != std::string::npos,
               "search JSON escapes quotes");
        expect(search->body.find("\\\\ test\\nline") != std::string::npos,
               "search JSON escapes backslash and newline");
        expect(search->body.find("\"visitorData\":\"test-visitor-data\"") != std::string::npos,
               "search body embeds guest client context");
    }

    const auto next_search = make_search_request(session, "ignored query", "NEXT_TOKEN");
    expect(next_search.has_value(), "search continuation request builds");
    if (next_search) {
        expect(next_search->body.find("\"continuation\":\"NEXT_TOKEN\"") != std::string::npos,
               "continuation token is passed opaquely");
        expect(next_search->body.find("ignored query") == std::string::npos,
               "continuation request does not resend the original query");
    }

    const auto home = make_home_request(session);
    expect(home.has_value(), "guest Home request builds");
    if (home) {
        expect(home->url.find("/browse?") != std::string::npos,
               "Home uses browse endpoint");
        expect(home->body.find("\"browseId\":\"FEwhat_to_watch\"") != std::string::npos,
               "Home uses the standard guest Home browse id");
    }

    GuestSession invalid_session;
    expect(!make_search_request(invalid_session, "anything"),
           "request builder rejects incomplete guest sessions");
    expect(!make_search_request(session, ""),
           "request builder rejects empty initial searches");
    expect(!make_browse_request(session, ""),
           "request builder rejects empty initial browse ids");

    expect(parse_settings(serialize_settings({true}))->show_fps, "settings round trip");
    expect(!parse_settings("version=1\nshow_fps=0\n")->show_fps, "disabled preference loads");
    expect(parse_settings("version=1\r\nshow_fps=1\r\n").has_value(), "CRLF settings accepted");
    expect(!parse_settings("version=1\nshow_fps="), "truncated settings rejected");
    expect(!parse_settings("version=2\nshow_fps=1\n"), "unknown version rejected");
    expect(!parse_settings("version=1\nshow_fps=yes\n"), "invalid boolean rejected");
    expect(!parse_settings("version=1\nshow_fps=1\nshow_fps=0\n"), "duplicate preference rejected");
    expect(!parse_settings("version=1\nshow_fps=0\nhide_shorts=0\n"), "content override rejected");
    expect(!parse_settings(std::string(513, 'x')), "oversized settings rejected");

    using namespace ttnx::ui;
    expect(kRootNavigation.size() == 5, "root navigation contains exactly five sections");
    expect(root_navigation_contains("Home"), "Home root section exists");
    expect(root_navigation_contains("Search"), "Search root section exists");
    expect(root_navigation_contains("Subscriptions"), "Subscriptions root section exists");
    expect(root_navigation_contains("Library"), "Library root section exists");
    expect(root_navigation_contains("Settings"), "Settings root section exists");
    expect(!root_navigation_contains("Shorts"), "Shorts can never become a root navigation section");

    if (failures == 0) {
        std::cout << "All TizenTube NX core tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
