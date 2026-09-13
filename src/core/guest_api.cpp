#include "tizentube_nx/youtube/guest_api.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ttnx::youtube {
namespace {

constexpr std::string_view kYouTubeOrigin = "https://www.youtube.com";
constexpr std::string_view kInnertubeBase = "https://www.youtube.com/youtubei/";
constexpr std::string_view kHomeBrowseId = "FEwhat_to_watch";

std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (const unsigned char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0x0f]);
                    out.push_back(hex[c & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string context_json(const GuestSession& session) {
    std::string out = "{\"client\":{";
    out += "\"clientName\":\"" + json_escape(session.client_name) + "\",";
    out += "\"clientVersion\":\"" + json_escape(session.client_version) + "\",";
    out += "\"hl\":\"" + json_escape(session.language) + "\",";
    out += "\"gl\":\"" + json_escape(session.region) + "\",";
    out += "\"visitorData\":\"" + json_escape(session.visitor_data) + "\",";
    out += "\"timeZone\":\"" + json_escape(session.timezone) + "\"";
    if (!session.user_agent.empty()) {
        out += ",\"userAgent\":\"" + json_escape(session.user_agent) + "\"";
    }
    out += "}}";
    return out;
}

net::HttpRequest innertube_post(
    const GuestSession& session,
    std::string_view endpoint,
    std::string payload_fields) {
    net::HttpRequest request;
    request.method = net::HttpMethod::Post;
    request.url = std::string(kInnertubeBase) + session.api_version + "/" +
                  std::string(endpoint) + "?prettyPrint=false&alt=json";
    request.headers = {
        {"Accept", "*/*"},
        {"Accept-Language", "*"},
        {"Content-Type", "application/json"},
        {"X-Goog-Visitor-Id", session.visitor_data},
        {"X-Youtube-Client-Version", session.client_version},
        {"Origin", std::string(kYouTubeOrigin)},
        {"Referer", std::string(kYouTubeOrigin) + "/"},
    };
    if (!session.client_name_id.empty()) {
        request.headers.push_back({"X-Youtube-Client-Name", session.client_name_id});
    }
    if (!session.user_agent.empty()) {
        request.headers.push_back({"User-Agent", session.user_agent});
    }

    request.body = "{\"context\":" + context_json(session);
    if (!payload_fields.empty()) {
        request.body += "," + payload_fields;
    }
    request.body += "}";
    return request;
}

std::string json_string_field(std::string_view name, std::string_view value) {
    return "\"" + std::string(name) + "\":\"" + json_escape(value) + "\"";
}

}  // namespace

net::HttpRequest make_session_bootstrap_request(
    std::string_view accept_language,
    std::string_view timezone,
    std::string_view user_agent,
    std::string_view visitor_cookie_id) {
    net::HttpRequest request;
    request.method = net::HttpMethod::Get;
    request.url = std::string(kYouTubeOrigin) + "/sw.js_data";
    request.headers = {
        {"Accept-Language", std::string(accept_language.empty() ? "en-US" : accept_language)},
        {"Accept", "*/*"},
        {"Referer", std::string(kYouTubeOrigin) + "/sw.js"},
    };
    if (!user_agent.empty()) {
        request.headers.push_back({"User-Agent", std::string(user_agent)});
    }

    std::string cookie = "PREF=tz=";
    for (const char c : timezone) {
        cookie.push_back(c == '/' ? '.' : c);
    }
    cookie += ";";
    if (!visitor_cookie_id.empty()) {
        cookie += "VISITOR_INFO1_LIVE=" + std::string(visitor_cookie_id) + ";";
    }
    request.headers.push_back({"Cookie", std::move(cookie)});
    return request;
}

std::optional<net::HttpRequest> make_search_request(
    const GuestSession& session,
    std::string_view query,
    std::string_view continuation) {
    if (!session.usable()) return std::nullopt;
    if (continuation.empty() && query.empty()) return std::nullopt;

    const auto fields = continuation.empty()
        ? json_string_field("query", query)
        : json_string_field("continuation", continuation);
    return innertube_post(session, "search", fields);
}

std::optional<net::HttpRequest> make_browse_request(
    const GuestSession& session,
    std::string_view browse_id,
    std::string_view continuation) {
    if (!session.usable()) return std::nullopt;
    if (continuation.empty() && browse_id.empty()) return std::nullopt;

    const auto fields = continuation.empty()
        ? json_string_field("browseId", browse_id)
        : json_string_field("continuation", continuation);
    return innertube_post(session, "browse", fields);
}

std::optional<net::HttpRequest> make_home_request(
    const GuestSession& session,
    std::string_view continuation) {
    return make_browse_request(session, kHomeBrowseId, continuation);
}

}  // namespace ttnx::youtube
