#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/core/settings.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
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

std::string header_value(
    const std::vector<ttnx::net::HttpHeader>& headers,
    const std::string& name) {
    for (const auto& header : headers) {
        if (header.name == name) return header.value;
    }
    return {};
}

}  // namespace

int main() {
    using namespace ttnx::core;

    expect(canonical_video_url("dQw4w9WgXcQ") ==
               "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
           "canonical watch URL");
    expect(canonical_video_url("https://youtu.be/dQw4w9WgXcQ?si=tracking") ==
               "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
           "tracking is stripped from pasted youtu.be URL");
    expect(canonical_video_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=10s&feature=share") ==
               "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
           "watch URL is rebuilt from video ID");
    expect(canonical_video_url("https://www.youtube.com/shorts/dQw4w9WgXcQ").empty(),
           "Shorts URLs are rejected rather than canonicalized");

    ContentItem normal{ContentKind::Video, "normal", "Normal"};
    ContentItem shorts{ContentKind::Short, "short", "Short"};
    ContentItem ad{ContentKind::Video, "ad", "Advertisement", true, false};
    ContentItem shopping{ContentKind::Video, "shop", "Shopping", false, true};

    expect(!should_hide(normal), "normal video survives default policy");
    expect(should_hide(shorts), "Shorts are always hidden");
    expect(should_hide(ad), "promoted content is always hidden");
    expect(should_hide(shopping), "shopping content hidden by default");

    FilterPolicy permissive;
    permissive.hide_shorts = false;
    permissive.hide_promoted = false;
    permissive.hide_shopping = false;
    expect(should_hide(shorts, permissive), "Shorts remain hidden even if preference says otherwise");
    expect(should_hide(ad, permissive), "promoted content remains hidden even if preference says otherwise");
    expect(!should_hide(shopping, permissive), "shopping policy may be relaxed separately");

    GuestRequest home;
    home.surface = BrowseSurface::Home;
    expect(valid_guest_request(home), "empty Home request is valid");

    GuestRequest search;
    search.surface = BrowseSurface::Search;
    search.value = "switch homebrew";
    expect(valid_guest_request(search), "Search request needs query text");
    search.value.clear();
    expect(!valid_guest_request(search), "empty first-page Search rejected");
    search.continuation = "opaque-token";
    expect(valid_guest_request(search), "continuation request may omit original query");

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
        {"videoRenderer", {ContentKind::Video, "video1", "Normal video"},
         "Channel", "", {}, "thumb", "12:34"},
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
           "bootstrap carries timezone and visitor cookie when supplied");

    GuestSession session;
    session.client_name = "WEB";
    session.client_name_id = "1";
    session.client_version = "2.20260901.00.00";
    session.visitor_data = "visitor-data";
    session.language = "en";
    session.region = "CA";
    session.timezone = "America/Toronto";
    session.user_agent = "TizenTubeNX-Test/1.0";

    const auto first_search = make_search_request(session, "mario kart");
    expect(first_search.has_value(), "first Search request built");
    expect(first_search && first_search->method == ttnx::net::HttpMethod::Post,
           "Search uses POST");
    expect(first_search && first_search->url ==
               "https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json",
           "Search targets InnerTube search");
    expect(first_search && first_search->body.find("\"query\":\"mario kart\"") != std::string::npos,
           "first Search request contains query");
    expect(first_search && first_search->body.find("\"continuation\"") == std::string::npos,
           "first Search request omits continuation");
    expect(first_search && header_value(first_search->headers, "X-Goog-Visitor-Id") == "visitor-data",
           "Search carries visitor header");

    const auto next_search = make_search_request(session, "must-not-resend", "opaque-token");
    expect(next_search.has_value(), "Search continuation request built");
    expect(next_search && next_search->body.find("\"continuation\":\"opaque-token\"") != std::string::npos,
           "Search continuation carries opaque token");
    expect(next_search && next_search->body.find("must-not-resend") == std::string::npos,
           "Search continuation does not resend original query");

    const auto home_request = make_home_request(session);
    expect(home_request.has_value(), "Home request built");
    expect(home_request && home_request->body.find("\"browseId\":\"FEwhat_to_watch\"") !=
               std::string::npos,
           "Home request uses what-to-watch browse id");

    const auto browse_continue = make_browse_request(session, "ignored", "browse-next");
    expect(browse_continue && browse_continue->body.find("\"continuation\":\"browse-next\"") !=
               std::string::npos,
           "Browse continuation carries token");
    expect(browse_continue && browse_continue->body.find("\"browseId\"") == std::string::npos,
           "Browse continuation does not resend browse id");

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
