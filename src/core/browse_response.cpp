#include "tizentube_nx/youtube/browse_response.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttnx::youtube {
namespace {

constexpr std::size_t kMaxSearchResponseBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxJsonNodes = 200000;
constexpr std::size_t kMaxRendererRecords = 1000;
constexpr unsigned kMaxJsonDepth = 128;

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void skip_ws(std::string_view input, std::size_t& pos) {
    while (pos < input.size() && is_ws(input[pos])) ++pos;
}

bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

unsigned hex_value(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    return static_cast<unsigned>(c - 'A' + 10);
}

bool append_utf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        if (codepoint >= 0xd800 && codepoint <= 0xdfff) return false;
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        return false;
    }
    return true;
}

std::optional<std::uint32_t> read_u16_escape(std::string_view input, std::size_t& pos) {
    if (pos + 4 > input.size()) return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!is_hex(input[pos + i])) return std::nullopt;
        value = (value << 4) | hex_value(input[pos + i]);
    }
    pos += 4;
    return value;
}

bool parse_string_at(std::string_view input, std::size_t& pos, std::string* decoded) {
    if (pos >= input.size() || input[pos] != '"') return false;
    ++pos;
    if (decoded) decoded->clear();

    while (pos < input.size()) {
        const unsigned char c = static_cast<unsigned char>(input[pos++]);
        if (c == '"') return true;
        if (c < 0x20) return false;
        if (c != '\\') {
            if (decoded) decoded->push_back(static_cast<char>(c));
            continue;
        }

        if (pos >= input.size()) return false;
        const char escape = input[pos++];
        switch (escape) {
            case '"': if (decoded) decoded->push_back('"'); break;
            case '\\': if (decoded) decoded->push_back('\\'); break;
            case '/': if (decoded) decoded->push_back('/'); break;
            case 'b': if (decoded) decoded->push_back('\b'); break;
            case 'f': if (decoded) decoded->push_back('\f'); break;
            case 'n': if (decoded) decoded->push_back('\n'); break;
            case 'r': if (decoded) decoded->push_back('\r'); break;
            case 't': if (decoded) decoded->push_back('\t'); break;
            case 'u': {
                auto first = read_u16_escape(input, pos);
                if (!first) return false;
                std::uint32_t codepoint = *first;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (pos + 2 > input.size() || input[pos] != '\\' || input[pos + 1] != 'u') {
                        return false;
                    }
                    pos += 2;
                    auto second = read_u16_escape(input, pos);
                    if (!second || *second < 0xdc00 || *second > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*second - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return false;
                }
                if (decoded && !append_utf8(*decoded, codepoint)) return false;
                break;
            }
            default:
                return false;
        }
    }
    return false;
}

bool skip_value_at(
    std::string_view input,
    std::size_t& pos,
    unsigned depth,
    std::size_t* node_count = nullptr) {
    if (depth > kMaxJsonDepth) return false;
    skip_ws(input, pos);
    if (pos >= input.size()) return false;
    if (node_count && ++(*node_count) > kMaxJsonNodes) return false;

    if (input[pos] == '"') return parse_string_at(input, pos, nullptr);

    if (input[pos] == '{') {
        ++pos;
        skip_ws(input, pos);
        if (pos < input.size() && input[pos] == '}') {
            ++pos;
            return true;
        }
        while (pos < input.size()) {
            if (!parse_string_at(input, pos, nullptr)) return false;
            skip_ws(input, pos);
            if (pos >= input.size() || input[pos++] != ':') return false;
            if (!skip_value_at(input, pos, depth + 1, node_count)) return false;
            skip_ws(input, pos);
            if (pos >= input.size()) return false;
            if (input[pos] == '}') {
                ++pos;
                return true;
            }
            if (input[pos++] != ',') return false;
            skip_ws(input, pos);
        }
        return false;
    }

    if (input[pos] == '[') {
        ++pos;
        skip_ws(input, pos);
        if (pos < input.size() && input[pos] == ']') {
            ++pos;
            return true;
        }
        while (pos < input.size()) {
            if (!skip_value_at(input, pos, depth + 1, node_count)) return false;
            skip_ws(input, pos);
            if (pos >= input.size()) return false;
            if (input[pos] == ']') {
                ++pos;
                return true;
            }
            if (input[pos++] != ',') return false;
            skip_ws(input, pos);
        }
        return false;
    }

    auto skip_literal = [&](std::string_view literal) {
        if (input.substr(pos, literal.size()) != literal) return false;
        pos += literal.size();
        return true;
    };
    if (input[pos] == 't') return skip_literal("true");
    if (input[pos] == 'f') return skip_literal("false");
    if (input[pos] == 'n') return skip_literal("null");

    const std::size_t start = pos;
    if (input[pos] == '-') ++pos;
    if (pos >= input.size()) return false;
    if (input[pos] == '0') {
        ++pos;
    } else if (input[pos] >= '1' && input[pos] <= '9') {
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
    } else {
        return false;
    }
    if (pos < input.size() && input[pos] == '.') {
        ++pos;
        const auto fraction_start = pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (fraction_start == pos) return false;
    }
    if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
        ++pos;
        if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
        const auto exponent_start = pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (exponent_start == pos) return false;
    }
    return pos > start;
}

bool validate_json(std::string_view input) {
    std::size_t pos = 0;
    std::size_t nodes = 0;
    if (!skip_value_at(input, pos, 0, &nodes)) return false;
    skip_ws(input, pos);
    return pos == input.size();
}

struct ObjectMember {
    std::string key;
    std::string_view value;
};

std::optional<std::vector<ObjectMember>> object_members(std::string_view object_json) {
    std::size_t pos = 0;
    skip_ws(object_json, pos);
    if (pos >= object_json.size() || object_json[pos++] != '{') return std::nullopt;
    skip_ws(object_json, pos);

    std::vector<ObjectMember> members;
    if (pos < object_json.size() && object_json[pos] == '}') return members;

    while (pos < object_json.size()) {
        std::string key;
        if (!parse_string_at(object_json, pos, &key)) return std::nullopt;
        skip_ws(object_json, pos);
        if (pos >= object_json.size() || object_json[pos++] != ':') return std::nullopt;
        skip_ws(object_json, pos);
        const auto value_start = pos;
        if (!skip_value_at(object_json, pos, 0)) return std::nullopt;
        members.push_back({std::move(key), object_json.substr(value_start, pos - value_start)});
        skip_ws(object_json, pos);
        if (pos >= object_json.size()) return std::nullopt;
        if (object_json[pos] == '}') {
            ++pos;
            skip_ws(object_json, pos);
            return pos == object_json.size() ? std::optional<std::vector<ObjectMember>>(std::move(members))
                                             : std::nullopt;
        }
        if (object_json[pos++] != ',') return std::nullopt;
        skip_ws(object_json, pos);
    }
    return std::nullopt;
}

std::optional<std::vector<std::string_view>> array_elements(std::string_view array_json) {
    std::size_t pos = 0;
    skip_ws(array_json, pos);
    if (pos >= array_json.size() || array_json[pos++] != '[') return std::nullopt;
    skip_ws(array_json, pos);

    std::vector<std::string_view> elements;
    if (pos < array_json.size() && array_json[pos] == ']') return elements;

    while (pos < array_json.size()) {
        const auto value_start = pos;
        if (!skip_value_at(array_json, pos, 0)) return std::nullopt;
        elements.push_back(array_json.substr(value_start, pos - value_start));
        skip_ws(array_json, pos);
        if (pos >= array_json.size()) return std::nullopt;
        if (array_json[pos] == ']') {
            ++pos;
            skip_ws(array_json, pos);
            return pos == array_json.size() ? std::optional<std::vector<std::string_view>>(std::move(elements))
                                            : std::nullopt;
        }
        if (array_json[pos++] != ',') return std::nullopt;
        skip_ws(array_json, pos);
    }
    return std::nullopt;
}

std::optional<std::string_view> object_field(std::string_view object_json, std::string_view wanted) {
    const auto members = object_members(object_json);
    if (!members) return std::nullopt;

    std::optional<std::string_view> result;
    for (const auto& member : *members) {
        if (member.key != wanted) continue;
        if (result) return std::nullopt;  // Duplicate requested fields fail closed.
        result = member.value;
    }
    return result;
}

std::optional<std::string_view> nested_field(
    std::string_view value,
    std::initializer_list<std::string_view> path) {
    std::optional<std::string_view> current = value;
    for (const auto key : path) {
        if (!current) return std::nullopt;
        current = object_field(*current, key);
    }
    return current;
}

std::optional<std::string_view> array_element_at(std::string_view array_json, std::size_t index) {
    const auto elements = array_elements(array_json);
    if (!elements || index >= elements->size()) return std::nullopt;
    return (*elements)[index];
}

std::optional<std::string> decode_json_string(std::string_view value) {
    std::size_t pos = 0;
    skip_ws(value, pos);
    std::string decoded;
    if (!parse_string_at(value, pos, &decoded)) return std::nullopt;
    skip_ws(value, pos);
    if (pos != value.size()) return std::nullopt;
    return decoded;
}

std::optional<std::string> string_field(std::string_view object_json, std::string_view key) {
    const auto value = object_field(object_json, key);
    return value ? decode_json_string(*value) : std::nullopt;
}

std::string text_node(std::string_view value) {
    if (const auto direct = decode_json_string(value)) return *direct;

    if (const auto simple = object_field(value, "simpleText")) {
        if (const auto decoded = decode_json_string(*simple)) return *decoded;
    }
    if (const auto content = object_field(value, "content")) {
        if (const auto decoded = decode_json_string(*content)) return *decoded;
    }

    const auto runs = object_field(value, "runs");
    if (!runs) return {};
    const auto elements = array_elements(*runs);
    if (!elements) return {};

    std::string combined;
    for (const auto element : *elements) {
        const auto text = string_field(element, "text");
        if (text) combined += *text;
    }
    return combined;
}

std::string text_field(std::string_view object_json, std::string_view key) {
    const auto value = object_field(object_json, key);
    return value ? text_node(*value) : std::string{};
}

std::string thumbnail_from_sources(std::string_view sources_json) {
    const auto elements = array_elements(sources_json);
    if (!elements) return {};
    std::string best;
    for (const auto element : *elements) {
        if (const auto url = string_field(element, "url"); url && !url->empty()) best = *url;
    }
    return best;
}

std::string classic_thumbnail(std::string_view renderer) {
    const auto thumbs = nested_field(renderer, {"thumbnail", "thumbnails"});
    return thumbs ? thumbnail_from_sources(*thumbs) : std::string{};
}

std::string lockup_thumbnail(std::string_view renderer) {
    const auto sources = nested_field(renderer, {"contentImage", "thumbnailViewModel", "image", "sources"});
    return sources ? thumbnail_from_sources(*sources) : std::string{};
}

std::string first_nonempty_text(
    std::string_view object_json,
    std::initializer_list<std::string_view> fields) {
    for (const auto field : fields) {
        const auto text = text_field(object_json, field);
        if (!text.empty()) return text;
    }
    return {};
}

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool contains_any(std::string_view value, std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
        if (value.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

bool looks_like_short(std::string_view renderer) {
    // Fail-safe markers observed across legacy and modern YouTube renderers.
    // A false positive only hides an item; it can never surface a Short.
    return contains_any(renderer, {
        "\"reelWatchEndpoint\"",
        "\"shortsLockupViewModel\"",
        "WEB_PAGE_TYPE_SHORTS",
        "\"url\":\"/shorts/",
        "\"url\": \"/shorts/",
    });
}

bool looks_live(std::string_view renderer) {
    return contains_any(renderer, {
        "BADGE_STYLE_TYPE_LIVE_NOW",
        "THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE",
        "\"style\":\"LIVE\"",
        "\"style\": \"LIVE\"",
    });
}

bool looks_promoted(std::string_view renderer) {
    return contains_any(renderer, {
        "\"promotedBadgeRenderer\"",
        "\"adBadgeRenderer\"",
        "\"isAd\":true",
        "\"isAd\": true",
    });
}

bool looks_shopping(std::string_view renderer) {
    return contains_any(renderer, {
        "\"shoppingOverlayRenderer\"",
        "\"productListRenderer\"",
        "\"merchandiseShelfRenderer\"",
    });
}

std::optional<core::RendererRecord> parse_video_renderer(std::string_view renderer) {
    const auto id = string_field(renderer, "videoId");
    const auto title = object_field(renderer, "title");
    if (!id || id->empty() || !title) {
        if (!looks_like_short(renderer)) return std::nullopt;
        core::RendererRecord blocked;
        blocked.renderer = "videoRenderer";
        blocked.item.kind = core::ContentKind::Short;
        return blocked;
    }

    core::RendererRecord record;
    record.renderer = "videoRenderer";
    record.item.id = *id;
    record.item.title = text_node(*title);
    record.item.kind = looks_like_short(renderer)
        ? core::ContentKind::Short
        : (looks_live(renderer) ? core::ContentKind::Live : core::ContentKind::Video);
    record.item.promoted = looks_promoted(renderer);
    record.item.shopping = looks_shopping(renderer);
    record.channel_title = first_nonempty_text(
        renderer, {"longBylineText", "ownerText", "shortBylineText"});
    record.thumbnail_url = classic_thumbnail(renderer);
    record.duration_text = text_field(renderer, "lengthText");
    return record;
}

std::optional<core::RendererRecord> parse_channel_renderer(std::string_view renderer) {
    const auto id = string_field(renderer, "channelId");
    const auto title = object_field(renderer, "title");
    if (!id || id->empty() || !title) return std::nullopt;

    core::RendererRecord record;
    record.renderer = "channelRenderer";
    record.item.kind = core::ContentKind::Channel;
    record.item.id = *id;
    record.item.title = text_node(*title);
    record.thumbnail_url = classic_thumbnail(renderer);
    return record;
}

std::optional<core::RendererRecord> parse_playlist_renderer(
    std::string_view renderer,
    std::string_view renderer_name) {
    auto id = string_field(renderer, "playlistId");
    if ((!id || id->empty())) {
        if (const auto watch = nested_field(renderer, {"navigationEndpoint", "watchEndpoint"})) {
            id = string_field(*watch, "playlistId");
        }
    }
    const auto title = object_field(renderer, "title");
    if (!id || id->empty() || !title) return std::nullopt;

    core::RendererRecord record;
    record.renderer = std::string(renderer_name);
    record.item.kind = core::ContentKind::Playlist;
    record.item.id = *id;
    record.item.title = text_node(*title);
    record.channel_title = first_nonempty_text(renderer, {"longBylineText", "shortBylineText"});
    record.thumbnail_url = classic_thumbnail(renderer);
    return record;
}

std::string lockup_channel_title(std::string_view renderer) {
    const auto metadata = nested_field(renderer, {"metadata", "lockupMetadataViewModel", "metadata",
                                                   "contentMetadataViewModel"});
    if (!metadata) return {};
    const auto rows = object_field(*metadata, "metadataRows");
    if (!rows) return {};
    const auto first_row = array_element_at(*rows, 0);
    if (!first_row) return {};
    const auto parts = object_field(*first_row, "metadataParts");
    if (!parts) return {};
    const auto first_part = array_element_at(*parts, 0);
    if (!first_part) return {};
    const auto text = object_field(*first_part, "text");
    return text ? text_node(*text) : std::string{};
}

std::optional<core::RendererRecord> parse_lockup_renderer(
    std::string_view renderer,
    std::string_view renderer_name) {
    const auto content_type = string_field(renderer, "contentType");
    const auto id = string_field(renderer, "contentId");

    core::RendererRecord record;
    record.renderer = std::string(renderer_name);
    record.item.id = id.value_or("");

    if (renderer_name.find("short") != std::string_view::npos || looks_like_short(renderer)) {
        record.item.kind = core::ContentKind::Short;
    } else if (content_type && *content_type == "LOCKUP_CONTENT_TYPE_VIDEO") {
        record.item.kind = looks_live(renderer) ? core::ContentKind::Live : core::ContentKind::Video;
    } else if (content_type && *content_type == "LOCKUP_CONTENT_TYPE_PLAYLIST") {
        record.item.kind = core::ContentKind::Playlist;
    } else if (content_type && *content_type == "LOCKUP_CONTENT_TYPE_SHORT") {
        record.item.kind = core::ContentKind::Short;
    } else {
        record.item.kind = core::ContentKind::Unknown;
    }

    if (const auto title = nested_field(renderer, {"metadata", "lockupMetadataViewModel", "title"})) {
        record.item.title = text_node(*title);
    }
    record.channel_title = lockup_channel_title(renderer);
    record.thumbnail_url = lockup_thumbnail(renderer);
    record.item.promoted = looks_promoted(renderer);
    record.item.shopping = looks_shopping(renderer);

    // Do not create a usable normal card without a stable content id and title.
    // Blocked/unknown records may still be returned internally so the renderer
    // firewall gets the final say and tests cover the hard invariants.
    if ((record.item.kind == core::ContentKind::Video ||
         record.item.kind == core::ContentKind::Live ||
         record.item.kind == core::ContentKind::Playlist) &&
        (record.item.id.empty() || record.item.title.empty())) {
        record.item.kind = core::ContentKind::Unknown;
    }
    return record;
}

bool is_short_subtree(std::string_view key) {
    const auto name = lowercase(key);
    return contains_any(name, {"short", "reel"});
}

bool is_promoted_subtree(std::string_view key) {
    const auto name = lowercase(key);
    return contains_any(name, {
        "promoted", "adslot", "displayad", "instreamad", "mastheadad",
        "searchpyv", "advertisement", "adplacement", "playerad",
    });
}

bool is_shopping_subtree(std::string_view key) {
    const auto name = lowercase(key);
    return contains_any(name, {"shopping", "product", "merchandise"});
}

core::RendererRecord blocked_record(std::string_view renderer, core::ContentKind kind) {
    core::RendererRecord record;
    record.renderer = std::string(renderer);
    record.item.kind = kind;
    return record;
}

std::optional<std::string> find_continuation_token(std::string_view value, unsigned depth = 0) {
    if (depth > kMaxJsonDepth) return std::nullopt;
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return std::nullopt;

    if (value[pos] == '{') {
        const auto members = object_members(value);
        if (!members) return std::nullopt;
        for (const auto& member : *members) {
            if (member.key == "continuationCommand") {
                const auto token = string_field(member.value, "token");
                if (token && !token->empty()) return token;
            }
        }
        for (const auto& member : *members) {
            if (const auto token = find_continuation_token(member.value, depth + 1)) return token;
        }
    } else if (value[pos] == '[') {
        const auto elements = array_elements(value);
        if (!elements) return std::nullopt;
        for (const auto element : *elements) {
            if (const auto token = find_continuation_token(element, depth + 1)) return token;
        }
    }
    return std::nullopt;
}

struct WalkState {
    std::vector<core::RendererRecord> records;
    std::string continuation;
    bool ambiguous_continuation{false};
    bool too_many_records{false};
};

void add_record(WalkState& state, core::RendererRecord record) {
    if (state.records.size() >= kMaxRendererRecords) {
        state.too_many_records = true;
        return;
    }
    state.records.push_back(std::move(record));
}

void add_continuation(WalkState& state, const std::optional<std::string>& token) {
    if (!token || token->empty()) return;
    if (state.continuation.empty()) {
        state.continuation = *token;
    } else if (state.continuation != *token) {
        state.ambiguous_continuation = true;
    }
}

bool walk_response(std::string_view value, WalkState& state, unsigned depth = 0) {
    if (depth > kMaxJsonDepth || state.too_many_records) return false;
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return false;

    if (value[pos] == '[') {
        const auto elements = array_elements(value);
        if (!elements) return false;
        for (const auto element : *elements) {
            if (!walk_response(element, state, depth + 1)) return false;
        }
        return true;
    }

    if (value[pos] != '{') return true;
    const auto members = object_members(value);
    if (!members) return false;

    for (const auto& member : *members) {
        if (member.key == "videoRenderer") {
            if (auto record = parse_video_renderer(member.value)) add_record(state, std::move(*record));
            continue;
        }
        if (member.key == "channelRenderer") {
            if (auto record = parse_channel_renderer(member.value)) add_record(state, std::move(*record));
            continue;
        }
        if (member.key == "playlistRenderer" || member.key == "radioRenderer") {
            if (auto record = parse_playlist_renderer(member.value, member.key)) {
                add_record(state, std::move(*record));
            }
            continue;
        }
        if (member.key == "lockupViewModel") {
            if (auto record = parse_lockup_renderer(member.value, member.key)) {
                add_record(state, std::move(*record));
            }
            continue;
        }
        if (member.key == "continuationItemRenderer" ||
            member.key == "continuationItemViewModel" ||
            member.key == "continuationItemView") {
            add_continuation(state, find_continuation_token(member.value));
            continue;
        }

        // Block these subtrees before recursion. This prevents a nested normal
        // videoRenderer from escaping an ad, shopping or Shorts container.
        if (is_short_subtree(member.key)) {
            add_record(state, blocked_record(member.key, core::ContentKind::Short));
            continue;
        }
        if (is_promoted_subtree(member.key)) {
            auto record = blocked_record(member.key, core::ContentKind::Unknown);
            record.item.promoted = true;
            add_record(state, std::move(record));
            continue;
        }
        if (is_shopping_subtree(member.key)) {
            auto record = blocked_record(member.key, core::ContentKind::Unknown);
            record.item.shopping = true;
            add_record(state, std::move(record));
            continue;
        }

        if (!walk_response(member.value, state, depth + 1)) return false;
    }
    return !state.too_many_records;
}

BrowseResponseParseResult fail(std::string error) {
    BrowseResponseParseResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

BrowseResponseParseResult parse_search_response(
    std::string_view response_body,
    const core::FilterPolicy& policy) {
    if (response_body.empty()) return fail("Search response is empty.");
    if (response_body.size() > kMaxSearchResponseBytes) {
        return fail("Search response is too large.");
    }
    if (!validate_json(response_body)) {
        return fail("Search response is malformed or exceeds parser limits.");
    }

    WalkState state;
    state.records.reserve(64);
    if (!walk_response(response_body, state)) {
        return fail(state.too_many_records
            ? "Search response contains too many renderer records."
            : "Search response traversal failed.");
    }

    // Multiple distinct continuation tokens in one recursively scanned response
    // are intentionally not guessed. First-page results remain usable, but
    // pagination is disabled until a single unambiguous token is available.
    if (state.ambiguous_continuation) state.continuation.clear();

    BrowseResponseParseResult result;
    result.page = core::sanitize_guest_page(
        std::move(state.records), std::move(state.continuation), policy);
    return result;
}

}  // namespace ttnx::youtube
