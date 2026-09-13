#include "tizentube_nx/youtube/search_response.hpp"

#include <cctype>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttnx::youtube {
namespace {

constexpr std::size_t kMaxSearchResponseBytes = 8 * 1024 * 1024;
constexpr unsigned kMaxScopeDepth = 128;
constexpr std::size_t kMaxScopeNodes = 200000;
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

bool skip_value(
    std::string_view input,
    std::size_t& pos,
    unsigned depth = 0,
    std::size_t* node_count = nullptr) {
    if (depth > kMaxScopeDepth) return false;
    skip_ws(input, pos);
    if (pos >= input.size()) return false;
    if (node_count && ++(*node_count) > kMaxScopeNodes) return false;

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
        while (pos < input.size()) {
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

bool validate_document(std::string_view input) {
    std::size_t pos = 0;
    std::size_t nodes = 0;
    if (!skip_value(input, pos, 0, &nodes)) return false;
    skip_ws(input, pos);
    return pos == input.size();
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
        // Trusted structural keys are ASCII and unescaped. Valid JSON with an
        // escaped key is accepted syntactically but cannot match a scope key.
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
            return pos == object_json.size()
                ? std::optional<std::vector<Member>>(std::move(output))
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

bool append_payload(
    std::vector<std::string_view>& payloads,
    std::string_view payload) {
    if (payloads.size() >= kMaxScopedPayloads) return false;
    payloads.push_back(payload);
    return true;
}

bool collect_continuation_payloads(
    std::string_view updates_json,
    std::vector<std::string_view>& payloads) {
    // Continuation scope is deliberately narrow: the recognized response fields
    // must be arrays, and only direct append/reload action objects may contribute
    // continuationItems. We never recursively hunt arbitrary command metadata.
    const auto updates = elements(updates_json);
    if (!updates) return false;

    for (const auto update : *updates) {
        std::size_t pos = 0;
        skip_ws(update, pos);
        if (pos >= update.size() || update[pos] != '{') continue;

        for (const auto action_key : {
                 std::string_view{"appendContinuationItemsAction"},
                 std::string_view{"reloadContinuationItemsCommand"}}) {
            const auto action = field(update, action_key);
            if (!action) continue;
            const auto continuation_items = field(*action, "continuationItems");
            if (continuation_items && !append_payload(payloads, *continuation_items)) return false;
        }
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
    if (response_body.empty()) return fail("Search response is empty.");
    if (response_body.size() > kMaxSearchResponseBytes) {
        return fail("Search response is too large.");
    }
    if (!validate_document(response_body)) {
        return fail("Search response is malformed or exceeds scope parser limits.");
    }

    std::vector<std::string_view> payloads;
    payloads.reserve(4);

    if (const auto primary = path(response_body, {
            "contents", "twoColumnSearchResultsRenderer", "primaryContents"})) {
        if (!append_payload(payloads, *primary)) {
            return fail("Search response contains too many scoped payloads.");
        }
    }

    for (const auto response_key : {
             std::string_view{"onResponseReceivedCommands"},
             std::string_view{"onResponseReceivedActions"},
             std::string_view{"onResponseReceivedEndpoints"}}) {
        if (const auto updates = field(response_body, response_key)) {
            if (!collect_continuation_payloads(*updates, payloads)) {
                return fail("Search continuation scope is malformed or exceeds limits.");
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

    // Only explicitly scoped Search payloads reach the reusable renderer walker.
    // Its existing Shorts/promoted/shopping/unknown firewall remains the final
    // content boundary inside those accepted containers.
    return parse_search_response(scoped, policy);
}

}  // namespace ttnx::youtube
