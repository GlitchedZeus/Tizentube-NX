#include "tizentube_nx/youtube/browse_response.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
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
constexpr std::size_t kMaxObjectMembers = 1024;
constexpr std::size_t kMaxArrayElements = 4096;
constexpr std::size_t kMaxDecodedStringBytes = 4096;
constexpr std::size_t kMaxExtractedTextBytes = 2048;
constexpr std::size_t kMaxTextRuns = 64;
constexpr std::size_t kMaxRawThumbnailCandidates = 16;
constexpr std::size_t kMaxThumbnailUrlBytes = 2048;
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

bool decoded_within_limit(const std::string* decoded) {
    return !decoded || decoded->size() <= kMaxDecodedStringBytes;
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
            if (!decoded_within_limit(decoded)) return false;
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
        if (!decoded_within_limit(decoded)) return false;
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
        if (members.size() >= kMaxObjectMembers) return std::nullopt;
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
            return pos == object_json.size()
                ? std::optional<std::vector<ObjectMember>>(std::move(members))
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
        if (elements.size() >= kMaxArrayElements) return std::nullopt;
        const auto value_start = pos;
        if (!skip_value_at(array_json, pos, 0)) return std::nullopt;
        elements.push_back(array_json.substr(value_start, pos - value_start));
        skip_ws(array_json, pos);
        if (pos >= array_json.size()) return std::nullopt;
        if (array_json[pos] == ']') {
            ++pos;
            skip_ws(array_json, pos);
            return pos == array_json.size()
                ? std::optional<std::vector<std::string_view>>(std::move(elements))
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

std::optional<std::uint64_t> parse_unsigned_json_integer(std::string_view value) {
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return std::nullopt;

    std::uint64_t result = 0;
    std::size_t digits = 0;
    while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos]))) {
        const auto digit = static_cast<unsigned>(value[pos] - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        result = result * 10 + digit;
        ++digits;
        ++pos;
    }
    skip_ws(value, pos);
    return digits > 0 && pos == value.size() ? std::optional<std::uint64_t>(result)
                                             : std::nullopt;
}

std::optional<std::uint64_t> unsigned_field(std::string_view object_json, std::string_view key) {
    const auto value = object_field(object_json, key);
    if (!value) return std::nullopt;
    if (const auto number = parse_unsigned_json_integer(*value)) return number;
    const auto text = decode_json_string(*value);
    return text ? parse_unsigned_json_integer(*text) : std::nullopt;
}

std::string bounded_text_node(std::string value) {
    return value.size() <= kMaxExtractedTextBytes ? std::move(value) : std::string{};
}

std::string text_node(std::string_view value) {
    if (const auto direct = decode_json_string(value)) return bounded_text_node(*direct);

    if (const auto simple = object_field(value, "simpleText")) {
        if (const auto decoded = decode_json_string(*simple)) return bounded_text_node(*decoded);
    }
    if (const auto content = object_field(value, "content")) {
        if (const auto decoded = decode_json_string(*content)) return bounded_text_node(*decoded);
    }

    const auto runs = object_field(value, "runs");
    if (!runs) return {};
    const auto elements = array_elements(*runs);
    if (!elements || elements->size() > kMaxTextRuns) return {};

    std::string combined;
    for (const auto element : *elements) {
        const auto text = string_field(element, "text");
        if (!text) continue;
        if (combined.size() > kMaxExtractedTextBytes - std::min(
                kMaxExtractedTextBytes, text->size())) {
            return {};
        }
        combined += *text;
        if (combined.size() > kMaxExtractedTextBytes) return {};
    }
    return combined;
}

std::string text_field(std::string_view object_json, std::string_view key) {
    const auto value = object_field(object_json, key);
    return value ? text_node(*value) : std::string{};
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

bool safe_thumbnail_url(std::string_view url) {
    if (url.empty() || url.size() > kMaxThumbnailUrlBytes || !url.starts_with("https://")) {
        return false;
    }
    for (const unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    return true;
}

void append_thumbnail_sources(
    std::vector<core::ThumbnailCandidate>& output,
    std::string_view sources_json) {
    const auto elements = array_elements(sources_json);
    if (!elements) return;

    for (const auto element : *elements) {
        if (output.size() >= kMaxRawThumbnailCandidates) return;
        const auto url = string_field(element, "url");
        if (!url || !safe_thumbnail_url(*url)) continue;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (const auto parsed = unsigned_field(element, "width");
            parsed && *parsed <= std::numeric_limits<std::uint32_t>::max()) {
            width = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto parsed = unsigned_field(element, "height");
            parsed && *parsed <= std::numeric_limits<std::uint32_t>::max()) {
            height = static_cast<std::uint32_t>(*parsed);
        }
        output.push_back({*url, width, height});
    }
}

void append_sources_path(
    std::vector<core::ThumbnailCandidate>& output,
    std::string_view renderer,
    std::initializer_list<std::string_view> path) {
    if (const auto sources = nested_field(renderer, path)) {
        append_thumbnail_sources(output, *sources);
    }
}

std::vector<core::ThumbnailCandidate> classic_thumbnails(std::string_view renderer) {
    std::vector<core::ThumbnailCandidate> output;
    output.reserve(4);
    append_sources_path(output, renderer, {"thumbnail", "thumbnails"});
    append_sources_path(output, renderer,
        {"thumbnailRenderer", "playlistVideoThumbnailRenderer", "thumbnail", "thumbnails"});
    return output;
}

std::vector<core::ThumbnailCandidate> lockup_thumbnails(std::string_view renderer) {
    std::vector<core::ThumbnailCandidate> output;
    output.reserve(4);
    append_sources_path(output, renderer,
        {"contentImage", "thumbnailViewModel", "image", "sources"});
    append_sources_path(output, renderer,
        {"contentImage", "decoratedAvatarViewModel", "avatar", "avatarViewModel", "image", "sources"});
    append_sources_path(output, renderer,
        {"contentImage", "collectionThumbnailViewModel", "primaryThumbnail",
         "thumbnailViewModel", "image", "sources"});
    return output;
}

std::string preferred_legacy_thumbnail(const std::vector<core::ThumbnailCandidate>& candidates) {
    return candidates.empty() ? std::string{} : candidates.back().url;
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

std::optional<std::string> unique_browse_id_from_text_node(std::string_view text_json) {
    std::optional<std::string> found;
    auto merge = [&](const std::optional<std::string>& candidate) {
        if (!candidate || candidate->empty()) return true;
        if (!found) {
            found = *candidate;
            return true;
        }
        return *found == *candidate;
    };

    if (const auto runs = object_field(text_json, "runs")) {
        const auto elements = array_elements(*runs);
        if (elements && elements->size() <= kMaxTextRuns) {
            for (const auto run : *elements) {
                const auto endpoint = nested_field(run, {"navigationEndpoint", "browseEndpoint"});
                if (endpoint && !merge(string_field(*endpoint, "browseId"))) return std::nullopt;
            }
        }
    }

    if (const auto command_runs = object_field(text_json, "commandRuns")) {
        const auto elements = array_elements(*command_runs);
        if (elements && elements->size() <= kMaxTextRuns) {
            for (const auto run : *elements) {
                const auto endpoint = nested_field(
                    run, {"onTap", "innertubeCommand", "browseEndpoint"});
                if (endpoint && !merge(string_field(*endpoint, "browseId"))) return std::nullopt;
            }
        }
    }
    return found;
}

std::string first_nonempty_browse_id(
    std::string_view renderer,
    std::initializer_list<std::string_view> fields) {
    std::optional<std::string> found;
    for (const auto field_name : fields) {
        const auto field = object_field(renderer, field_name);
        if (!field) continue;
        const auto candidate = unique_browse_id_from_text_node(*field);
        if (!candidate || candidate->empty()) continue;
        if (found && *found != *candidate) return {};
        found = *candidate;
    }
    return found.value_or("");
}

std::string accessibility_label(std::string_view value) {
    for (const auto path : {
            std::initializer_list<std::string_view>{"accessibility", "accessibilityData", "label"},
            std::initializer_list<std::string_view>{"accessibilityData", "label"}}) {
        if (const auto label = nested_field(value, path)) {
            if (const auto decoded = decode_json_string(*label)) return bounded_text_node(*decoded);
        }
    }
    if (const auto accessibility_text = object_field(value, "accessibilityText")) {
        return text_node(*accessibility_text);
    }
    return {};
}

std::optional<std::uint64_t> parse_duration_seconds(std::string_view text) {
    if (text.empty() || text.size() > 32) return std::nullopt;
    std::uint64_t total = 0;
    std::uint64_t part = 0;
    unsigned components = 1;
    bool have_digit = false;

    for (const char c : text) {
        if (c == ':') {
            if (!have_digit || components >= 3 || part >= 60 && components > 1) return std::nullopt;
            if (total > (std::numeric_limits<std::uint64_t>::max() - part) / 60) {
                return std::nullopt;
            }
            total = total * 60 + part;
            part = 0;
            have_digit = false;
            ++components;
            continue;
        }
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<unsigned>(c - '0');
        if (part > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        part = part * 10 + digit;
        have_digit = true;
    }
    if (!have_digit || (components > 1 && part >= 60)) return std::nullopt;
    if (total > (std::numeric_limits<std::uint64_t>::max() - part) / 60) {
        return std::nullopt;
    }
    return total * 60 + part;
}

std::optional<std::uint64_t> parse_exact_count_text(std::string_view text) {
    while (!text.empty() && is_ws(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_ws(text.back())) text.remove_suffix(1);
    if (text.empty() || text.size() > 64) return std::nullopt;

    const auto space = text.find(' ');
    const auto number = space == std::string_view::npos ? text : text.substr(0, space);
    const auto suffix = space == std::string_view::npos ? std::string_view{} : text.substr(space + 1);
    if (!suffix.empty()) {
        const auto lower = lowercase(suffix);
        if (lower != "view" && lower != "views" && lower != "video" &&
            lower != "videos" && lower != "subscriber" && lower != "subscribers") {
            return std::nullopt;
        }
    }

    std::uint64_t value = 0;
    std::size_t group_digits = 0;
    std::size_t group_index = 0;
    bool saw_comma = false;
    for (const char c : number) {
        if (c == ',') {
            if (group_digits == 0) return std::nullopt;
            if ((group_index == 0 && group_digits > 3) || (group_index > 0 && group_digits != 3)) {
                return std::nullopt;
            }
            saw_comma = true;
            ++group_index;
            group_digits = 0;
            continue;
        }
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<unsigned>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
        ++group_digits;
    }
    if (group_digits == 0) return std::nullopt;
    if (saw_comma && group_index > 0 && group_digits != 3) return std::nullopt;
    return value;
}

bool looks_like_short(std::string_view renderer) {
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

bool looks_upcoming(std::string_view renderer) {
    return contains_any(renderer, {
        "\"upcomingEventData\"",
        "BADGE_STYLE_TYPE_UPCOMING",
        "THUMBNAIL_OVERLAY_STYLE_UPCOMING",
        "THUMBNAIL_OVERLAY_BADGE_STYLE_UPCOMING",
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

std::optional<std::uint64_t> unique_unsigned_field_recursive(
    std::string_view value,
    std::string_view wanted,
    unsigned depth = 0) {
    if (depth > 12) return std::nullopt;
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return std::nullopt;

    std::optional<std::uint64_t> found;
    auto merge = [&](const std::optional<std::uint64_t>& candidate) {
        if (!candidate) return true;
        if (!found) {
            found = *candidate;
            return true;
        }
        return *found == *candidate;
    };

    if (value[pos] == '{') {
        const auto members = object_members(value);
        if (!members) return std::nullopt;
        for (const auto& member : *members) {
            if (member.key == wanted) {
                std::optional<std::uint64_t> candidate = parse_unsigned_json_integer(member.value);
                if (!candidate) {
                    if (const auto decoded = decode_json_string(member.value)) {
                        candidate = parse_unsigned_json_integer(*decoded);
                    }
                }
                if (!merge(candidate)) return std::nullopt;
            }
        }
        for (const auto& member : *members) {
            const auto candidate = unique_unsigned_field_recursive(member.value, wanted, depth + 1);
            if (candidate && !merge(candidate)) return std::nullopt;
        }
    } else if (value[pos] == '[') {
        const auto elements = array_elements(value);
        if (!elements) return std::nullopt;
        for (const auto element : *elements) {
            const auto candidate = unique_unsigned_field_recursive(element, wanted, depth + 1);
            if (candidate && !merge(candidate)) return std::nullopt;
        }
    }
    return found;
}

struct LockupPart {
    std::string text;
    std::string browse_id;
    std::string accessibility;
};

std::optional<LockupPart> lockup_part(
    std::string_view renderer,
    std::size_t row_index,
    std::size_t part_index) {
    const auto metadata = nested_field(renderer, {
        "metadata", "lockupMetadataViewModel", "metadata", "contentMetadataViewModel"});
    if (!metadata) return std::nullopt;
    const auto rows = object_field(*metadata, "metadataRows");
    if (!rows) return std::nullopt;
    const auto row = array_element_at(*rows, row_index);
    if (!row) return std::nullopt;
    const auto parts = object_field(*row, "metadataParts");
    if (!parts) return std::nullopt;
    const auto part = array_element_at(*parts, part_index);
    if (!part) return std::nullopt;
    const auto text = object_field(*part, "text");
    if (!text) return std::nullopt;

    LockupPart result;
    result.text = text_node(*text);
    result.browse_id = unique_browse_id_from_text_node(*text).value_or("");
    result.accessibility = accessibility_label(*text);
    return result;
}

std::string lockup_accessibility(std::string_view renderer) {
    if (const auto title = nested_field(renderer, {"metadata", "lockupMetadataViewModel", "title"})) {
        const auto label = accessibility_label(*title);
        if (!label.empty()) return label;
    }
    if (const auto image = nested_field(renderer, {"contentImage", "thumbnailViewModel"})) {
        const auto label = accessibility_label(*image);
        if (!label.empty()) return label;
    }
    return {};
}

std::string find_duration_text_recursive(std::string_view value, unsigned depth = 0) {
    if (depth > 10) return {};
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return {};

    if (value[pos] == '{') {
        const auto members = object_members(value);
        if (!members) return {};
        for (const auto& member : *members) {
            if (member.key == "text" || member.key == "content" || member.key == "label") {
                const auto candidate = text_node(member.value);
                if (parse_duration_seconds(candidate)) return candidate;
            }
        }
        for (const auto& member : *members) {
            const auto candidate = find_duration_text_recursive(member.value, depth + 1);
            if (!candidate.empty()) return candidate;
        }
    } else if (value[pos] == '[') {
        const auto elements = array_elements(value);
        if (!elements) return {};
        for (const auto element : *elements) {
            const auto candidate = find_duration_text_recursive(element, depth + 1);
            if (!candidate.empty()) return candidate;
        }
    }
    return {};
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
    record.channel_id = first_nonempty_browse_id(
        renderer, {"longBylineText", "ownerText", "shortBylineText"});

    record.thumbnails = classic_thumbnails(renderer);
    record.thumbnail_url = preferred_legacy_thumbnail(record.thumbnails);
    record.duration_text = text_field(renderer, "lengthText");
    record.duration_seconds = parse_duration_seconds(record.duration_text);
    record.view_count_text = first_nonempty_text(renderer, {"viewCountText", "shortViewCountText"});
    record.view_count = parse_exact_count_text(record.view_count_text);
    record.published_text = text_field(renderer, "publishedTimeText");
    record.accessibility_text = accessibility_label(*title);
    record.upcoming = looks_upcoming(renderer);
    if (record.upcoming) {
        record.scheduled_start_time_seconds = unique_unsigned_field_recursive(renderer, "startTime");
    }
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
    record.channel_id = *id;
    record.thumbnails = classic_thumbnails(renderer);
    record.thumbnail_url = preferred_legacy_thumbnail(record.thumbnails);
    record.subscriber_count_text = text_field(renderer, "subscriberCountText");
    record.subscriber_count = parse_exact_count_text(record.subscriber_count_text);
    record.video_count_text = text_field(renderer, "videoCountText");
    record.video_count = parse_exact_count_text(record.video_count_text);
    record.accessibility_text = accessibility_label(*title);
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
    record.channel_id = first_nonempty_browse_id(renderer, {"longBylineText", "shortBylineText"});
    record.thumbnails = classic_thumbnails(renderer);
    record.thumbnail_url = preferred_legacy_thumbnail(record.thumbnails);
    record.video_count_text = text_field(renderer, "videoCountText");
    record.video_count = parse_exact_count_text(record.video_count_text);
    record.accessibility_text = accessibility_label(*title);
    return record;
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
    } else if (content_type && *content_type == "LOCKUP_CONTENT_TYPE_CHANNEL") {
        record.item.kind = core::ContentKind::Channel;
    } else if (content_type && *content_type == "LOCKUP_CONTENT_TYPE_SHORT") {
        record.item.kind = core::ContentKind::Short;
    } else {
        record.item.kind = core::ContentKind::Unknown;
    }

    if (const auto title = nested_field(renderer, {"metadata", "lockupMetadataViewModel", "title"})) {
        record.item.title = text_node(*title);
    }

    record.thumbnails = lockup_thumbnails(renderer);
    record.thumbnail_url = preferred_legacy_thumbnail(record.thumbnails);
    record.accessibility_text = lockup_accessibility(renderer);
    record.item.promoted = looks_promoted(renderer);
    record.item.shopping = looks_shopping(renderer);
    record.upcoming = looks_upcoming(renderer);
    if (record.upcoming) {
        record.scheduled_start_time_seconds = unique_unsigned_field_recursive(renderer, "startTime");
    }

    const auto row0_part0 = lockup_part(renderer, 0, 0);
    const auto row0_part1 = lockup_part(renderer, 0, 1);
    const auto row0_part2 = lockup_part(renderer, 0, 2);
    const auto row1_part0 = lockup_part(renderer, 1, 0);
    const auto row1_part1 = lockup_part(renderer, 1, 1);

    if (record.item.kind == core::ContentKind::Video || record.item.kind == core::ContentKind::Live) {
        if (row0_part0) {
            record.channel_title = row0_part0->text;
            record.channel_id = row0_part0->browse_id;
        }
        record.view_count_text = row1_part0 && !row1_part0->text.empty()
            ? row1_part0->text
            : (row0_part1 ? row0_part1->text : std::string{});
        record.view_count = parse_exact_count_text(record.view_count_text);
        record.published_text = row1_part1 && !row1_part1->text.empty()
            ? row1_part1->text
            : (row0_part2 ? row0_part2->text : std::string{});

        if (const auto content_image = object_field(renderer, "contentImage")) {
            record.duration_text = find_duration_text_recursive(*content_image);
            record.duration_seconds = parse_duration_seconds(record.duration_text);
        }
    } else if (record.item.kind == core::ContentKind::Playlist) {
        if (row0_part0) {
            record.channel_title = row0_part0->text;
            record.channel_id = row0_part0->browse_id;
        }
        record.video_count_text = row1_part0 && !row1_part0->text.empty()
            ? row1_part0->text
            : (row0_part1 ? row0_part1->text : std::string{});
        record.video_count = parse_exact_count_text(record.video_count_text);
    } else if (record.item.kind == core::ContentKind::Channel) {
        record.channel_id = record.item.id;
        record.subscriber_count_text = row0_part0 ? row0_part0->text : std::string{};
        record.subscriber_count = parse_exact_count_text(record.subscriber_count_text);
        record.video_count_text = row1_part0 ? row1_part0->text : std::string{};
        record.video_count = parse_exact_count_text(record.video_count_text);
    }

    // Do not create a usable normal card without a stable content id and title.
    // Blocked/unknown records may still be returned internally so the renderer
    // firewall gets the final say and tests cover the hard invariants.
    if ((record.item.kind == core::ContentKind::Video ||
         record.item.kind == core::ContentKind::Live ||
         record.item.kind == core::ContentKind::Playlist ||
         record.item.kind == core::ContentKind::Channel) &&
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
