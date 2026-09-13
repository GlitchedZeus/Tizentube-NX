#include "tizentube_nx/youtube/home_response.hpp"

#include "tizentube_nx/core/search_result.hpp"
#include "tizentube_nx/youtube/browse_response.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ttnx::youtube {
namespace {

constexpr std::size_t kMaxHomeResponseBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxScopeNodes = 200000;
constexpr std::size_t kMaxObjectMembers = 1024;
constexpr std::size_t kMaxArrayElements = 4096;
constexpr std::size_t kMaxSectionTitleBytes = 256;
constexpr unsigned kMaxScopeDepth = 128;

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

bool parse_string_at(std::string_view input, std::size_t& pos, std::string* decoded = nullptr) {
    if (pos >= input.size() || input[pos++] != '"') return false;
    if (decoded) decoded->clear();

    while (pos < input.size()) {
        const unsigned char c = static_cast<unsigned char>(input[pos++]);
        if (c == '"') return true;
        if (c < 0x20) return false;
        if (c != '\\') {
            if (decoded) {
                decoded->push_back(static_cast<char>(c));
                if (decoded->size() > kMaxSectionTitleBytes * 8) return false;
            }
            continue;
        }
        if (pos >= input.size()) return false;
        const char escaped = input[pos++];
        switch (escaped) {
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
        if (decoded && decoded->size() > kMaxSectionTitleBytes * 8) return false;
    }
    return false;
}

bool skip_value(
    std::string_view input,
    std::size_t& pos,
    unsigned depth = 0,
    std::size_t* node_count = nullptr) {
    if (depth > kMaxScopeDepth) return false;
    skip_ws(input, pos);
    if (pos >= input.size()) return false;
    if (node_count && ++(*node_count) > kMaxScopeNodes) return false;

    if (input[pos] == '"') return parse_string_at(input, pos);

    if (input[pos] == '{') {
        ++pos;
        skip_ws(input, pos);
        if (pos < input.size() && input[pos] == '}') {
            ++pos;
            return true;
        }
        std::size_t members = 0;
        while (pos < input.size()) {
            if (++members > kMaxObjectMembers) return false;
            if (!parse_string_at(input, pos)) return false;
            skip_ws(input, pos);
            if (pos >= input.size() || input[pos++] != ':') return false;
            if (!skip_value(input, pos, depth + 1, node_count)) return false;
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
        std::size_t elements = 0;
        while (pos < input.size()) {
            if (++elements > kMaxArrayElements) return false;
            if (!skip_value(input, pos, depth + 1, node_count)) return false;
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

    auto literal = [&](std::string_view word) {
        if (input.substr(pos, word.size()) != word) return false;
        pos += word.size();
        return true;
    };
    if (input[pos] == 't') return literal("true");
    if (input[pos] == 'f') return literal("false");
    if (input[pos] == 'n') return literal("null");

    const auto start = pos;
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
        const auto digits = pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (digits == pos) return false;
    }
    if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
        ++pos;
        if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
        const auto digits = pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (digits == pos) return false;
    }
    return pos > start;
}

bool validate_document(std::string_view input) {
    std::size_t pos = 0;
    std::size_t nodes = 0;
    if (!skip_value(input, pos, 0, &nodes)) return false;
    skip_ws(input, pos);
    return pos == input.size();
}

struct Member {
    std::string key;
    std::string_view value;
};

std::optional<std::vector<Member>> object_members(std::string_view object_json) {
    std::size_t pos = 0;
    skip_ws(object_json, pos);
    if (pos >= object_json.size() || object_json[pos++] != '{') return std::nullopt;
    skip_ws(object_json, pos);

    std::vector<Member> output;
    if (pos < object_json.size() && object_json[pos] == '}') return output;

    while (pos < object_json.size()) {
        if (output.size() >= kMaxObjectMembers) return std::nullopt;
        std::string key;
        if (!parse_string_at(object_json, pos, &key)) return std::nullopt;
        skip_ws(object_json, pos);
        if (pos >= object_json.size() || object_json[pos++] != ':') return std::nullopt;
        skip_ws(object_json, pos);
        const auto value_start = pos;
        if (!skip_value(object_json, pos)) return std::nullopt;
        output.push_back({std::move(key), object_json.substr(value_start, pos - value_start)});
        skip_ws(object_json, pos);
        if (pos >= object_json.size()) return std::nullopt;
        if (object_json[pos] == '}') {
            ++pos;
            skip_ws(object_json, pos);
            return pos == object_json.size()
                ? std::optional<std::vector<Member>>(std::move(output))
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

    std::vector<std::string_view> output;
    if (pos < array_json.size() && array_json[pos] == ']') return output;

    while (pos < array_json.size()) {
        if (output.size() >= kMaxArrayElements) return std::nullopt;
        const auto start = pos;
        if (!skip_value(array_json, pos)) return std::nullopt;
        output.push_back(array_json.substr(start, pos - start));
        skip_ws(array_json, pos);
        if (pos >= array_json.size()) return std::nullopt;
        if (array_json[pos] == ']') {
            ++pos;
            skip_ws(array_json, pos);
            return pos == array_json.size()
                ? std::optional<std::vector<std::string_view>>(std::move(output))
                : std::nullopt;
        }
        if (array_json[pos++] != ',') return std::nullopt;
        skip_ws(array_json, pos);
    }
    return std::nullopt;
}

std::optional<std::string_view> field(std::string_view object_json, std::string_view wanted) {
    const auto members = object_members(object_json);
    if (!members) return std::nullopt;
    std::optional<std::string_view> found;
    for (const auto& member : *members) {
        if (member.key != wanted) continue;
        if (found) return std::nullopt;
        found = member.value;
    }
    return found;
}

std::optional<std::string_view> path(
    std::string_view root,
    std::initializer_list<std::string_view> keys) {
    std::optional<std::string_view> current = root;
    for (const auto key : keys) {
        if (!current) return std::nullopt;
        current = field(*current, key);
    }
    return current;
}

std::optional<std::string> decode_json_string(std::string_view value) {
    std::size_t pos = 0;
    skip_ws(value, pos);
    std::string decoded;
    if (!parse_string_at(value, pos, &decoded)) return std::nullopt;
    skip_ws(value, pos);
    return pos == value.size() ? std::optional<std::string>(std::move(decoded)) : std::nullopt;
}

std::string text_node(std::string_view value) {
    if (const auto direct = decode_json_string(value)) {
        return direct->size() <= kMaxSectionTitleBytes ? *direct : std::string{};
    }
    if (const auto simple = field(value, "simpleText")) {
        if (const auto decoded = decode_json_string(*simple)) {
            return decoded->size() <= kMaxSectionTitleBytes ? *decoded : std::string{};
        }
    }
    if (const auto content = field(value, "content")) {
        if (const auto decoded = decode_json_string(*content)) {
            return decoded->size() <= kMaxSectionTitleBytes ? *decoded : std::string{};
        }
    }
    const auto runs = field(value, "runs");
    if (!runs) return {};
    const auto items = array_elements(*runs);
    if (!items || items->size() > 64) return {};
    std::string out;
    for (const auto item : *items) {
        const auto text = field(item, "text");
        const auto decoded = text ? decode_json_string(*text) : std::nullopt;
        if (!decoded) continue;
        if (out.size() + decoded->size() > kMaxSectionTitleBytes) return {};
        out += *decoded;
    }
    return out;
}

bool json_true(std::string_view value) {
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (value.substr(pos, 4) != "true") return false;
    pos += 4;
    skip_ws(value, pos);
    return pos == value.size();
}

bool tab_selected(std::string_view tab_renderer) {
    const auto selected = field(tab_renderer, "selected");
    return selected && json_true(*selected);
}

std::optional<std::string_view> recognized_tab_contents(std::string_view tab_renderer) {
    const auto content = field(tab_renderer, "content");
    if (!content) return std::nullopt;
    if (const auto rich_grid = field(*content, "richGridRenderer")) {
        return field(*rich_grid, "contents");
    }
    if (const auto section_list = field(*content, "sectionListRenderer")) {
        return field(*section_list, "contents");
    }
    return std::nullopt;
}

std::string section_title(std::string_view entry) {
    for (const auto candidate : {
             path(entry, {"richSectionRenderer", "content", "richShelfRenderer", "title"}),
             path(entry, {"richSectionRenderer", "content", "shelfRenderer", "title"}),
             path(entry, {"richShelfRenderer", "title"}),
             path(entry, {"shelfRenderer", "title"}),
             path(entry, {"itemSectionRenderer", "header", "itemSectionHeaderRenderer", "title"}),
         }) {
        if (!candidate) continue;
        const auto text = text_node(*candidate);
        if (!text.empty()) return text;
    }
    return {};
}

HomeResponseParseResult fail(std::string error) {
    HomeResponseParseResult result;
    result.error = std::move(error);
    return result;
}

bool merge_continuation(std::string& current, bool& ambiguous, const std::string& candidate) {
    if (candidate.empty()) return true;
    if (current.empty()) {
        current = candidate;
        return true;
    }
    if (current != candidate) {
        ambiguous = true;
        return false;
    }
    return true;
}

bool append_normalized_payload(
    core::HomePage& home,
    std::unordered_set<std::string>& seen,
    std::string title,
    std::string_view payload,
    const core::FilterPolicy& policy,
    std::string& continuation,
    bool& ambiguous_continuation,
    std::string& error) {
    auto parsed = parse_search_response(payload, policy);
    if (!parsed || !parsed.page) {
        error = parsed.error.empty()
            ? "Home scoped renderer payload was rejected."
            : parsed.error;
        return false;
    }

    (void)merge_continuation(
        continuation,
        ambiguous_continuation,
        parsed.page->continuation);

    std::vector<core::BrowseResult> accepted;
    accepted.reserve(parsed.page->results.size());
    for (auto& result : parsed.page->results) {
        const auto identity = core::browse_result_identity(result);
        if (identity.empty() || !seen.insert(identity).second) continue;
        home.results.push_back(result);
        accepted.push_back(std::move(result));
    }

    if (accepted.empty()) return true;
    if (title.size() > kMaxSectionTitleBytes) title.clear();

    if (title.empty() && !home.sections.empty() && home.sections.back().title.empty()) {
        auto& existing = home.sections.back().results;
        existing.insert(
            existing.end(),
            std::make_move_iterator(accepted.begin()),
            std::make_move_iterator(accepted.end()));
    } else {
        core::HomeSection section;
        section.title = std::move(title);
        section.results = std::move(accepted);
        home.sections.push_back(std::move(section));
    }
    return true;
}

std::optional<std::string_view> browse_tabs(std::string_view response_body) {
    if (const auto two = path(response_body, {"contents", "twoColumnBrowseResultsRenderer", "tabs"})) {
        return two;
    }
    if (const auto one = path(response_body, {"contents", "singleColumnBrowseResultsRenderer", "tabs"})) {
        return one;
    }
    return std::nullopt;
}

bool collect_primary_home(
    std::string_view response_body,
    core::HomePage& home,
    std::unordered_set<std::string>& seen,
    const core::FilterPolicy& policy,
    std::string& continuation,
    bool& ambiguous_continuation,
    bool& saw_payload,
    std::string& error) {
    const auto tabs_json = browse_tabs(response_body);
    if (!tabs_json) return true;
    const auto tabs = array_elements(*tabs_json);
    if (!tabs) {
        error = "Home tab list is malformed or exceeds limits.";
        return false;
    }

    struct TabCandidate {
        bool selected{false};
        std::string_view contents;
    };
    std::vector<TabCandidate> candidates;
    for (const auto tab : *tabs) {
        const auto renderer = field(tab, "tabRenderer");
        if (!renderer) continue;
        const auto contents = recognized_tab_contents(*renderer);
        if (!contents) continue;
        candidates.push_back({tab_selected(*renderer), *contents});
    }
    if (candidates.empty()) return true;

    std::size_t chosen = 0;
    bool found_selected = false;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].selected) continue;
        if (found_selected) {
            error = "Home response contains multiple selected browse tabs.";
            return false;
        }
        chosen = i;
        found_selected = true;
    }

    const auto entries = array_elements(candidates[chosen].contents);
    if (!entries) {
        error = "Home result container is malformed or exceeds limits.";
        return false;
    }
    saw_payload = true;
    for (const auto entry : *entries) {
        if (!append_normalized_payload(
                home,
                seen,
                section_title(entry),
                entry,
                policy,
                continuation,
                ambiguous_continuation,
                error)) {
            return false;
        }
    }
    return true;
}

bool collect_continuation_home(
    std::string_view response_body,
    core::HomePage& home,
    std::unordered_set<std::string>& seen,
    const core::FilterPolicy& policy,
    std::string& continuation,
    bool& ambiguous_continuation,
    bool& saw_payload,
    std::string& error) {
    for (const auto response_key : {
             std::string_view{"onResponseReceivedActions"},
             std::string_view{"onResponseReceivedCommands"},
             std::string_view{"onResponseReceivedEndpoints"}}) {
        const auto updates_json = field(response_body, response_key);
        if (!updates_json) continue;
        const auto updates = array_elements(*updates_json);
        if (!updates) {
            error = "Home continuation action list is malformed or exceeds limits.";
            return false;
        }
        for (const auto update : *updates) {
            for (const auto action_key : {
                     std::string_view{"appendContinuationItemsAction"},
                     std::string_view{"reloadContinuationItemsCommand"}}) {
                const auto action = field(update, action_key);
                if (!action) continue;
                const auto items = field(*action, "continuationItems");
                if (!items) continue;
                saw_payload = true;
                if (!append_normalized_payload(
                        home,
                        seen,
                        {},
                        *items,
                        policy,
                        continuation,
                        ambiguous_continuation,
                        error)) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace

HomeResponseParseResult parse_scoped_home_response(
    std::string_view response_body,
    const core::FilterPolicy& policy) {
    if (response_body.empty()) return fail("Home response is empty.");
    if (response_body.size() > kMaxHomeResponseBytes) {
        return fail("Home response is too large.");
    }
    if (!validate_document(response_body)) {
        return fail("Home response is malformed or exceeds scope parser limits.");
    }

    core::HomePage home;
    std::unordered_set<std::string> seen;
    seen.reserve(128);
    std::string continuation;
    bool ambiguous_continuation = false;
    bool saw_payload = false;
    std::string error;

    if (!collect_primary_home(
            response_body,
            home,
            seen,
            policy,
            continuation,
            ambiguous_continuation,
            saw_payload,
            error)) {
        return fail(error.empty() ? "Home primary scope could not be read safely." : std::move(error));
    }
    if (!collect_continuation_home(
            response_body,
            home,
            seen,
            policy,
            continuation,
            ambiguous_continuation,
            saw_payload,
            error)) {
        return fail(error.empty() ? "Home continuation scope could not be read safely." : std::move(error));
    }

    if (!saw_payload) {
        return fail("Home response did not contain a recognized Home browse payload.");
    }

    if (!ambiguous_continuation) home.continuation = std::move(continuation);

    HomeResponseParseResult result;
    result.page = std::move(home);
    return result;
}

}  // namespace ttnx::youtube
