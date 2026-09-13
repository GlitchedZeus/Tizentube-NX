#include "tizentube_nx/youtube/search_response.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttnx::youtube {
namespace {

constexpr unsigned kMaxScopeDepth = 128;
constexpr std::size_t kMaxScopedPayloads = 64;

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void skip_ws(std::string_view input, std::size_t& pos) {
    while (pos < input.size() && is_ws(input[pos])) ++pos;
}

bool skip_string(std::string_view input, std::size_t& pos) {
    if (pos >= input.size() || input[pos++] != '"') return false;
    while (pos < input.size()) {
        const unsigned char c = static_cast<unsigned char>(input[pos++]);
        if (c == '"') return true;
        if (c < 0x20) return false;
        if (c != '\\') continue;
        if (pos >= input.size()) return false;
        const char escaped = input[pos++];
        if (escaped == 'u') {
            if (pos + 4 > input.size()) return false;
            for (std::size_t i = 0; i < 4; ++i) {
                if (!std::isxdigit(static_cast<unsigned char>(input[pos + i]))) return false;
            }
            pos += 4;
        } else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                   escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                   escaped != 'r' && escaped != 't') {
            return false;
        }
    }
    return false;
}

bool skip_value(std::string_view input, std::size_t& pos, unsigned depth = 0) {
    if (depth > kMaxScopeDepth) return false;
    skip_ws(input, pos);
    if (pos >= input.size()) return false;

    if (input[pos] == '"') return skip_string(input, pos);

    if (input[pos] == '{') {
        ++pos;
        skip_ws(input, pos);
        if (pos < input.size() && input[pos] == '}') {
            ++pos;
            return true;
        }
        while (pos < input.size()) {
            if (!skip_string(input, pos)) return false;
            skip_ws(input, pos);
            if (pos >= input.size() || input[pos++] != ':') return false;
            if (!skip_value(input, pos, depth + 1)) return false;
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
            if (!skip_value(input, pos, depth + 1)) return false;
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
        if (pos == digits) return false;
    }
    if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
        ++pos;
        if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
        const auto digits = pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (pos == digits) return false;
    }
    return pos > start;
}

struct Member {
    std::string_view key;
    std::string_view value;
};

std::optional<std::vector<Member>> members(std::string_view object_json) {
    std::size_t pos = 0;
    skip_ws(object_json, pos);
    if (pos >= object_json.size() || object_json[pos++] != '{') return std::nullopt;
    skip_ws(object_json, pos);

    std::vector<Member> output;
    if (pos < object_json.size() && object_json[pos] == '}') return output;

    while (pos < object_json.size()) {
        if (object_json[pos] != '"') return std::nullopt;
        const auto key_quote = pos;
        if (!skip_string(object_json, pos)) return std::nullopt;
        const auto key_end_quote = pos - 1;
        const auto key = object_json.substr(key_quote + 1, key_end_quote - key_quote - 1);
        // Structural YouTube field names are ASCII and are not escaped. An
        // escaped key is still valid JSON but deliberately cannot match one of
        // our trusted structural names.
        const bool key_has_escape = key.find('\\') != std::string_view::npos;

        skip_ws(object_json, pos);
        if (pos >= object_json.size() || object_json[pos++] != ':') return std::nullopt;
        skip_ws(object_json, pos);
        const auto value_start = pos;
        if (!skip_value(object_json, pos)) return std::nullopt;
        output.push_back({key_has_escape ? std::string_view{} : key,
                          object_json.substr(value_start, pos - value_start)});

        skip_ws(object_json, pos);
        if (pos >= object_json.size()) return std::nullopt;
        if (object_json[pos] == '}') {
            ++pos;
            skip_ws(object_json, pos);
            return pos == object_json.size() ? std::optional<std::vector<Member>>(std::move(output))
                                             : std::nullopt;
        }
        if (object_json[pos++] != ',') return std::nullopt;
        skip_ws(object_json, pos);
    }
    return std::nullopt;
}

std::optional<std::vector<std::string_view>> elements(std::string_view array_json) {
    std::size_t pos = 0;
    skip_ws(array_json, pos);
    if (pos >= array_json.size() || array_json[pos++] != '[') return std::nullopt;
    skip_ws(array_json, pos);

    std::vector<std::string_view> output;
    if (pos < array_json.size() && array_json[pos] == ']') return output;

    while (pos < array_json.size()) {
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
    const auto object_members = members(object_json);
    if (!object_members) return std::nullopt;
    std::optional<std::string_view> found;
    for (const auto& member : *object_members) {
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

bool collect_continuation_payloads(
    std::string_view value,
    std::vector<std::string_view>& payloads,
    unsigned depth = 0) {
    if (depth > kMaxScopeDepth || payloads.size() > kMaxScopedPayloads) return false;
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return false;

    if (value[pos] == '[') {
        const auto array = elements(value);
        if (!array) return false;
        for (const auto item : *array) {
            if (!collect_continuation_payloads(item, payloads, depth + 1)) return false;
        }
        return payloads.size() <= kMaxScopedPayloads;
    }

    if (value[pos] != '{') return true;
    const auto object_members = members(value);
    if (!object_members) return false;
    for (const auto& member : *object_members) {
        if (member.key == "appendContinuationItemsAction" ||
            member.key == "reloadContinuationItemsCommand") {
            if (const auto continuation_items = field(member.value, "continuationItems")) {
                payloads.push_back(*continuation_items);
                if (payloads.size() > kMaxScopedPayloads) return false;
            }
            continue;
        }
        if (!collect_continuation_payloads(member.value, payloads, depth + 1)) return false;
    }
    return true;
}

BrowseResponseParseResult fail(std::string error) {
    BrowseResponseParseResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

BrowseResponseParseResult parse_scoped_search_response(
    std::string_view response_body,
    const core::FilterPolicy& policy) {
    // Reuse the bounded low-level parser as a strict whole-document syntax and
    // size/depth/node validation pass. Its page is intentionally discarded.
    const auto validation = parse_search_response(response_body, policy);
    if (!validation) return validation;

    std::vector<std::string_view> payloads;
    payloads.reserve(4);

    if (const auto primary = path(response_body, {
            "contents", "twoColumnSearchResultsRenderer", "primaryContents"})) {
        payloads.push_back(*primary);
    }

    for (const auto response_key : {
             std::string_view{"onResponseReceivedCommands"},
             std::string_view{"onResponseReceivedActions"},
             std::string_view{"onResponseReceivedEndpoints"}}) {
        if (const auto updates = field(response_body, response_key)) {
            if (!collect_continuation_payloads(*updates, payloads)) {
                return fail("Search continuation payload traversal failed or exceeded limits.");
            }
        }
    }

    if (payloads.empty()) {
        return fail("Search response did not contain a recognized primary or continuation payload.");
    }

    std::string scoped;
    scoped.reserve(response_body.size() + 32);
    scoped += "{\"scopedSearchPayloads\":[";
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        if (i != 0) scoped.push_back(',');
        scoped.append(payloads[i]);
    }
    scoped += "]}";

    return parse_search_response(scoped, policy);
}

}  // namespace ttnx::youtube
