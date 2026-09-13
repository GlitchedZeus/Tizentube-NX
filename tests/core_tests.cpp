#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/guest_browse.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include "tizentube_nx/youtube/session_bootstrap.hpp"

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

std::string make_bootstrap_fixture(bool visitor_is_string = true) {
    std::vector<std::string> fields(108, "null");
    fields[0] = "\"en-US\"";
    fields[1] = "\"CA\"";
    // Exercise JSON unicode decoding while keeping the resulting visitor token ASCII.
    fields[13] = visitor_is_string ? "\"visitor\\u002Ddata\"" : "123";
    fields[16] = "\"2.20260912.01.00\"";
    fields[79] = "\"America/Toronto\"";

    std::string device_info = "[";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) device_info += ',';
        device_info += fields[i];
    }
    device_info += ']';

    // root[0][2] = ytcfg; ytcfg[0][0] = device_info.
    return ")]}'\n[[null,null,[[" + device_info + "],\"unused-api-key\"]]]";
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
           "session bootstrap normalizes timezone and carries visitor cookie id");

    const auto parsed_bootstrap = parse_session_bootstrap(make_bootstrap_fixture());
    expect(parsed_bootstrap.session.has_value(),
           "valid JSPB session bootstrap fixture parses");
    if (parsed_bootstrap.session) {
        expect(parsed_bootstrap.session->client_name == "WEB",
               "bootstrap parser selects guest WEB client");
        expect(parsed_bootstrap.session->client_version == "2.20260912.01.00",
               "bootstrap parser extracts web client version");
        expect(parsed_bootstrap.session->visitor_data == "visitor-data",
               "bootstrap parser extracts and decodes visitor data");
        expect(parsed_bootstrap.session->language == "en-US",
               "bootstrap parser uses server language when not overridden");
        expect(parsed_bootstrap.session->region == "CA",
               "bootstrap parser uses server region when not overridden");
        expect(parsed_bootstrap.session->timezone == "America/Toronto",
               "bootstrap parser uses server timezone when not overridden");
    }

    SessionBootstrapOptions bootstrap_options;
    bootstrap_options.language = "fr-CA";
    bootstrap_options.region = "CA";
    bootstrap_options.timezone = "America/Vancouver";
    bootstrap_options.user_agent = "TizenTubeNX-Test/2.0";
    const auto overridden_bootstrap = parse_session_bootstrap(make_bootstrap_fixture(), bootstrap_options);
    expect(overridden_bootstrap.session.has_value(),
           "bootstrap parser accepts explicit locale/timezone options");
    if (overridden_bootstrap.session) {
        expect(overridden_bootstrap.session->language == "fr-CA",
               "explicit language overrides server bootstrap language");
        expect(overridden_bootstrap.session->timezone == "America/Vancouver",
               "explicit timezone overrides server bootstrap timezone");
        expect(overridden_bootstrap.session->user_agent == "TizenTubeNX-Test/2.0",
               "bootstrap parser preserves caller user agent");
    }

    expect(!parse_session_bootstrap("[[null]]").session,
           "bootstrap parser rejects missing JSPB prefix");
    expect(!parse_session_bootstrap(")]}'\n[[null,null").session,
           "bootstrap parser rejects truncated JSON");
    expect(!parse_session_bootstrap(make_bootstrap_fixture(false)).session,
           "bootstrap parser rejects non-string visitor data");
    expect(!parse_session_bootstrap(std::string(4 * 1024 * 1024 + 1, 'x')).session,
           "bootstrap parser rejects oversized responses before parsing");

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
