#include "tizentube_nx/youtube/home_response.hpp"
#include "tizentube_nx/youtube/channel_response.hpp"
#include "tizentube_nx/core/url.hpp"

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
constexpr std::size_t kMaxDiagnosticFamilies = 16;
constexpr std::size_t kMaxDiagnosticFamilyBytes = 64;
constexpr std::size_t kMaxDiagnosticSummaryBytes = 1600;
constexpr unsigned kMaxDiagnosticDepth = 32;

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

bool has_suffix(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
 value.substr(value.size() - suffix.size()) == suffix;
}

bool diagnostic_family_name(std::string_view name) {
    if (name.empty() || name.size() > kMaxDiagnosticFamilyBytes) return false;
    if (!(has_suffix(name, "Renderer") || has_suffix(name, "ViewModel") ||
has_suffix(name, "View"))) {
        return false;
    }
    for (const unsigned char c : name) {
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

std::string ascii_lower(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool reviewed_diagnostic_wrapper(std::string_view name) {
    for (const auto reviewed : {
   std::string_view{"richGridRenderer"},
   std::string_view{"sectionListRenderer"},
   std::string_view{"richItemRenderer"},
   std::string_view{"richSectionRenderer"},
   std::string_view{"richShelfRenderer"},
   std::string_view{"shelfRenderer"},
   std::string_view{"itemSectionRenderer"},
   std::string_view{"itemSectionHeaderRenderer"},
   std::string_view{"horizontalListRenderer"},
   std::string_view{"expandedShelfContentsRenderer"}}) {
        if (name == reviewed) return true;
    }
    return false;
}

bool reviewed_diagnostic_leaf(std::string_view name) {
    for (const auto reviewed : {
   std::string_view{"videoRenderer"},
   std::string_view{"channelRenderer"},
   std::string_view{"playlistRenderer"},
   std::string_view{"radioRenderer"},
   std::string_view{"lockupViewModel"},
   // Physically observed guest Home empty-state leaf. Diagnostics may
   // name it, but must never descend through it.
   std::string_view{"feedNudgeRenderer"},
   std::string_view{"continuationItemRenderer"},
   std::string_view{"continuationItemViewModel"},
   std::string_view{"continuationItemView"}}) {
        if (name == reviewed) return true;
    }
    return false;
}

bool diagnostic_structural_key(std::string_view key) {
    for (const auto structural : {
   std::string_view{"content"},
   std::string_view{"contents"},
   std::string_view{"items"},
   std::string_view{"header"},
   std::string_view{"footer"},
   std::string_view{"continuationItems"}}) {
        if (key == structural) return true;
    }
    return false;
}

struct DiagnosticFamilyCount {
    std::string name;
    std::size_t count{0};
};

struct HomeDiagnosticsBuilder {
    bool two_column{false};
    bool single_column{false};
    std::size_t tab_count{0};
    std::size_t selected_tab_count{0};
    std::size_t shorts_reels{0};
    std::size_t ads_promoted{0};
    std::size_t shopping_product{0};
    std::size_t premium_promo{0};
    std::size_t continuation_renderers{0};
    std::size_t opaque_wrappers{0};
    std::size_t truncated_families{0};
    std::vector<DiagnosticFamilyCount> families;
    std::vector<std::string> opaque_family_names;

    bool observe_family(std::string_view name) {
        if (!diagnostic_family_name(name)) return false;
        bool found = false;
        for (auto& family : families) {
  if (family.name == name) {
      ++family.count;
      found = true;
      break;
  }
        }
        if (!found) {
  if (families.size() < kMaxDiagnosticFamilies) {
      families.push_back({std::string(name), 1});
  } else {
      ++truncated_families;
  }
        }

        const auto lower = ascii_lower(name);
        bool blocked = false;
        if (lower.find("reel") != std::string::npos ||
  lower.find("short") != std::string::npos) {
  ++shorts_reels;
  blocked = true;
        }
        if (lower.rfind("ad", 0) == 0 ||
  lower.find("promoted") != std::string::npos ||
  lower.find("sponsored") != std::string::npos) {
  ++ads_promoted;
  blocked = true;
        }
        if (lower.find("shopping") != std::string::npos ||
  lower.find("product") != std::string::npos ||
  lower.find("merch") != std::string::npos) {
  ++shopping_product;
  blocked = true;
        }
        if (lower.find("premium") != std::string::npos ||
  lower.find("promo") != std::string::npos ||
  lower.find("upsell") != std::string::npos ||
  lower.find("mealbar") != std::string::npos) {
  ++premium_promo;
  blocked = true;
        }
        if (lower.rfind("continuationitem", 0) == 0) {
  ++continuation_renderers;
        }
        return blocked;
    }

    void note_opaque(std::string_view name) {
        ++opaque_wrappers;
        if (!diagnostic_family_name(name)) return;
        for (const auto& existing : opaque_family_names) {
  if (existing == name) return;
        }
        if (opaque_family_names.size() < kMaxDiagnosticFamilies) {
  opaque_family_names.emplace_back(name);
        }
    }

    std::string summary() const {
        std::string out;
        auto append = [&out](std::string_view value) {
  if (out.size() >= kMaxDiagnosticSummaryBytes) return;
  const auto room = kMaxDiagnosticSummaryBytes - out.size();
  out.append(value.substr(0, room));
        };
        append("Home diag: twoColumn=");
        append(two_column ? "1" : "0");
        append(" singleColumn=");
        append(single_column ? "1" : "0");
        append(" tabs=");
        append(std::to_string(tab_count));
        append(" selected=");
        append(std::to_string(selected_tab_count));
        append(" | families=");
        if (families.empty()) {
  append("(none)");
        } else {
  for (std::size_t i = 0; i < families.size(); ++i) {
      if (i) append(",");
      append(families[i].name);
      append(":");
      append(std::to_string(families[i].count));
  }
        }
        if (truncated_families) {
  append(",+truncated:");
  append(std::to_string(truncated_families));
        }
        append(" | blocked shorts/reels=");
        append(std::to_string(shorts_reels));
        append(" ads/promoted=");
        append(std::to_string(ads_promoted));
        append(" shopping/product=");
        append(std::to_string(shopping_product));
        append(" premium/promo=");
        append(std::to_string(premium_promo));
        append(" continuationRenderers=");
        append(std::to_string(continuation_renderers));
        append(" opaque=");
        append(std::to_string(opaque_wrappers));
        if (!opaque_family_names.empty()) {
  append(" | opaqueFamilies=");
  for (std::size_t i = 0; i < opaque_family_names.size(); ++i) {
      if (i) append(",");
      append(opaque_family_names[i]);
  }
        }
        return out;
    }
};

void scan_home_structure(
    std::string_view value,
    HomeDiagnosticsBuilder& diagnostics,
    unsigned depth = 0) {
    if (depth > kMaxDiagnosticDepth) return;
    std::size_t pos = 0;
    skip_ws(value, pos);
    if (pos >= value.size()) return;

    if (value[pos] == '[') {
        const auto items = array_elements(value);
        if (!items) return;
        for (const auto item : *items) {
  scan_home_structure(item, diagnostics, depth + 1);
        }
        return;
    }
    if (value[pos] != '{') return;

    const auto members = object_members(value);
    if (!members) return;
    for (const auto& member : *members) {
        if (diagnostic_family_name(member.key)) {
  const bool blocked = diagnostics.observe_family(member.key);
  if (blocked) {
      diagnostics.note_opaque(member.key);
      continue;
  }
  if (reviewed_diagnostic_wrapper(member.key)) {
      scan_home_structure(member.value, diagnostics, depth + 1);
      continue;
  }
  if (reviewed_diagnostic_leaf(member.key)) {
      continue;
  }
  diagnostics.note_opaque(member.key);
  continue;
        }
        if (diagnostic_structural_key(member.key)) {
  scan_home_structure(member.value, diagnostics, depth + 1);
        }
    }
}

HomeDiagnosticsBuilder build_home_diagnostics(std::string_view response_body) {
    HomeDiagnosticsBuilder diagnostics;
    const auto two = path(response_body, {"contents", "twoColumnBrowseResultsRenderer", "tabs"});
    const auto one = path(response_body, {"contents", "singleColumnBrowseResultsRenderer", "tabs"});
    diagnostics.two_column = two.has_value();
    diagnostics.single_column = one.has_value();

    const auto tabs_json = two ? two : one;
    if (tabs_json) {
        if (const auto tabs = array_elements(*tabs_json)) {
  diagnostics.tab_count = tabs->size();
  std::optional<std::string_view> fallback_content;
  for (const auto tab : *tabs) {
      const auto renderer = field(tab, "tabRenderer");
      if (!renderer) continue;
      const auto content = field(*renderer, "content");
      if (content && !fallback_content) fallback_content = *content;
      if (!tab_selected(*renderer)) continue;
      ++diagnostics.selected_tab_count;
      if (content) scan_home_structure(*content, diagnostics);
  }
  if (diagnostics.selected_tab_count == 0 && fallback_content) {
      scan_home_structure(*fallback_content, diagnostics);
  }
        }
    }

    for (const auto response_key : {
   std::string_view{"onResponseReceivedActions"},
   std::string_view{"onResponseReceivedCommands"},
   std::string_view{"onResponseReceivedEndpoints"}}) {
        const auto updates_json = field(response_body, response_key);
        if (!updates_json) continue;
        const auto updates = array_elements(*updates_json);
        if (!updates) continue;
        for (const auto update : *updates) {
  for (const auto action_key : {
           std::string_view{"appendContinuationItemsAction"},
           std::string_view{"reloadContinuationItemsCommand"}}) {
      const auto action = field(update, action_key);
      if (!action) continue;
      const auto items = field(*action, "continuationItems");
      if (items) scan_home_structure(*items, diagnostics);
  }
        }
    }
    return diagnostics;
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

HomeResponseParseResult fail(std::string error, std::string diagnostics = {}) {
    HomeResponseParseResult result;
    result.error = std::move(error);
    result.diagnostics = std::move(diagnostics);
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

bool is_selected_home_feed_nudge_entry(std::string_view entry) {
    // Deliberately recognize only the physical selected-Home position:
    // richGrid contents -> richSectionRenderer -> content -> feedNudgeRenderer.
    // The renderer is terminal non-content; its payload is never inspected.
    const auto rich_section = field(entry, "richSectionRenderer");
    if (!rich_section) return false;
    const auto content = field(*rich_section, "content");
    return content && field(*content, "feedNudgeRenderer").has_value();
}

bool append_home_entry(
    core::HomePage& home,
    std::unordered_set<std::string>& seen,
    std::string inherited_title,
    std::string_view entry,
    const core::FilterPolicy& policy,
    std::string& continuation,
    bool& ambiguous_continuation,
    std::string& error) {
    // Home is deliberately fail-closed at renderer-family boundaries. Only
    // recognized structural wrappers are opened. Unknown/promo/Premium/command
    // renderers remain opaque even if they contain a video-shaped descendant.
    if (const auto rich_item = field(entry, "richItemRenderer")) {
        const auto content = field(*rich_item, "content");
        return !content || append_home_entry(
            home, seen, std::move(inherited_title), *content, policy,
            continuation, ambiguous_continuation, error);
    }

    if (const auto rich_section = field(entry, "richSectionRenderer")) {
        const auto content = field(*rich_section, "content");
        if (!content) return true;
        const auto title = section_title(entry);
        if (const auto shelf = field(*content, "richShelfRenderer")) {
            const auto contents = field(*shelf, "contents");
            if (!contents) return true;
            const auto items = array_elements(*contents);
            if (!items) {
                error = "Home shelf contents are malformed or exceed limits.";
                return false;
            }
            for (const auto item : *items) {
                if (!append_home_entry(
                        home, seen, title, item, policy,
                        continuation, ambiguous_continuation, error)) {
                    return false;
                }
            }
            return true;
        }
        if (const auto shelf = field(*content, "shelfRenderer")) {
            return append_home_entry(
                home, seen, title, *shelf, policy,
                continuation, ambiguous_continuation, error);
        }
        // reelShelfRenderer and every other unreviewed section type are opaque.
        return true;
    }

    if (const auto item_section = field(entry, "itemSectionRenderer")) {
        const auto contents = field(*item_section, "contents");
        if (!contents) return true;
        const auto items = array_elements(*contents);
        if (!items) {
            error = "Home item-section contents are malformed or exceed limits.";
            return false;
        }
        const auto title = section_title(entry);
        for (const auto item : *items) {
            if (!append_home_entry(
                    home, seen, title, item, policy,
                    continuation, ambiguous_continuation, error)) {
                return false;
            }
        }
        return true;
    }

    if (const auto rich_shelf = field(entry, "richShelfRenderer")) {
        const auto contents = field(*rich_shelf, "contents");
        if (!contents) return true;
        const auto items = array_elements(*contents);
        if (!items) {
            error = "Home rich-shelf contents are malformed or exceed limits.";
            return false;
        }
        const auto title = section_title(entry).empty()
            ? inherited_title
            : section_title(entry);
        for (const auto item : *items) {
            if (!append_home_entry(
                    home, seen, title, item, policy,
                    continuation, ambiguous_continuation, error)) {
                return false;
            }
        }
        return true;
    }

    if (const auto shelf = field(entry, "shelfRenderer")) {
        std::optional<std::string_view> items;
        if (const auto content = field(*shelf, "content")) {
            if (const auto horizontal = field(*content, "horizontalListRenderer")) {
                items = field(*horizontal, "items");
            } else if (const auto expanded = field(*content, "expandedShelfContentsRenderer")) {
                items = field(*expanded, "items");
            }
        }
        if (!items) items = field(*shelf, "contents");
        if (!items) return true;
        const auto children = array_elements(*items);
        if (!children) {
            error = "Home shelf item list is malformed or exceeds limits.";
            return false;
        }
        const auto title = section_title(entry).empty()
            ? inherited_title
            : section_title(entry);
        for (const auto child : *children) {
            if (!append_home_entry(
                    home, seen, title, child, policy,
                    continuation, ambiguous_continuation, error)) {
                return false;
            }
        }
        return true;
    }

    // These are the only leaf renderer families reviewed for the first Home
    // slice. The shared parser still applies the hard Shorts/ad/shopping filter
    // and metadata bounds inside each recognized leaf.
    for (const auto key : {
             std::string_view{"videoRenderer"},
             std::string_view{"channelRenderer"},
             std::string_view{"playlistRenderer"},
             std::string_view{"radioRenderer"},
             std::string_view{"lockupViewModel"},
             std::string_view{"continuationItemRenderer"},
             std::string_view{"continuationItemViewModel"},
             std::string_view{"continuationItemView"}}) {
        if (field(entry, key)) {
            return append_normalized_payload(
                home, seen, std::move(inherited_title), entry, policy,
                continuation, ambiguous_continuation, error);
        }
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
        // A feed nudge is a valid YouTube-provided empty Home state, not
        // a content wrapper. Recognition is confined to this selected
        // primary Home array; continuation/unknown/blocked scopes cannot
        // set the typed reason.
        if (is_selected_home_feed_nudge_entry(entry)) {
            home.empty_reason = core::HomeEmptyReason::FeedNudge;
            continue;
        }
        if (!append_home_entry(
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
                const auto children = array_elements(*items);
                if (!children) {
                    error = "Home continuation items are malformed or exceed limits.";
                    return false;
                }
                saw_payload = true;
                for (const auto child : *children) {
                    if (!append_home_entry(
                            home,
                            seen,
                            {},
                            child,
                            policy,
                            continuation,
                            ambiguous_continuation,
                            error)) {
                        return false;
                    }
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

    const std::string diagnostics = build_home_diagnostics(response_body).summary();

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
        return fail(error.empty() ? "Home primary scope could not be read safely." : std::move(error), diagnostics);
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
        return fail(error.empty() ? "Home continuation scope could not be read safely." : std::move(error), diagnostics);
    }

    if (!saw_payload) {
        return fail("Home response did not contain a recognized Home browse payload.", diagnostics);
    }

    // Normal content wins if YouTube sends both content and a nudge. A
    // nudge-only Home is terminal and cannot expose pagination.
    if (!home.results.empty()) {
        home.empty_reason = core::HomeEmptyReason::None;
    } else if (home.empty_reason == core::HomeEmptyReason::FeedNudge) {
        continuation.clear();
        ambiguous_continuation = false;
    }
    if (!ambiguous_continuation) home.continuation = std::move(continuation);

    HomeResponseParseResult result;
    result.page = std::move(home);
    result.diagnostics = diagnostics;
    return result;
}


namespace {

ChannelResponseParseResult channel_fail(std::string error, std::string diagnostics = {}) {
    ChannelResponseParseResult result;
    result.error = std::move(error);
    result.diagnostics = std::move(diagnostics);
    return result;
}

std::string channel_diagnostics(std::string_view response_body) {
    auto summary = build_home_diagnostics(response_body).summary();
    constexpr std::string_view prefix = "Home diag:";
    if (summary.starts_with(prefix)) {
        summary.replace(0, prefix.size(), "Channel diag:");
    }
    return summary;
}

std::string decoded_field(std::string_view object, std::string_view key) {
    const auto value = field(object, key);
    if (!value) return {};
    const auto decoded = decode_json_string(*value);
    return decoded ? *decoded : std::string{};
}

std::string bounded_channel_text(std::string value, std::size_t max_bytes) {
    if (value.size() > max_bytes) value.clear();
    return value;
}

bool apply_channel_metadata(
    std::string_view response_body,
    std::string_view expected_channel_id,
    core::ChannelPage& channel,
    std::string& error) {
    channel.identity.browse_id = std::string(expected_channel_id);
    if (const auto canonical = core::canonical_channel_url(expected_channel_id)) {
        channel.metadata.canonical_url = *canonical;
    }

    if (const auto metadata = path(response_body, {"metadata", "channelMetadataRenderer"})) {
        const auto external_id = decoded_field(*metadata, "externalId");
        if (!external_id.empty() && external_id != expected_channel_id) {
            error = "Channel metadata identity does not match the requested channel.";
            return false;
        }
        channel.metadata.title = bounded_channel_text(decoded_field(*metadata, "title"), 512);
        channel.metadata.description = bounded_channel_text(decoded_field(*metadata, "description"), 1024);

        const auto vanity = decoded_field(*metadata, "vanityChannelUrl");
        const auto marker = vanity.find("/@");
        if (marker != std::string::npos) {
            auto handle = vanity.substr(marker + 1);
            if (handle.size() <= 128 && handle.starts_with('@')) {
                bool safe = true;
                for (const unsigned char c : handle) {
                    if (c <= 0x20 || c == 0x7f || c == '/' || c == '?' || c == '#') {
                        safe = false;
                        break;
                    }
                }
                if (safe) channel.metadata.handle = std::move(handle);
            }
        }
    }

    if (const auto header = path(response_body, {"header", "c4TabbedHeaderRenderer"})) {
        const auto header_id = decoded_field(*header, "channelId");
        if (!header_id.empty() && header_id != expected_channel_id) {
            error = "Channel header identity does not match the requested channel.";
            return false;
        }
        if (channel.metadata.title.empty()) {
            channel.metadata.title = bounded_channel_text(decoded_field(*header, "title"), 512);
        }
        if (const auto handle = field(*header, "channelHandleText")) {
            channel.metadata.handle = bounded_channel_text(text_node(*handle), 128);
        }
        if (const auto subscribers = field(*header, "subscriberCountText")) {
            channel.metadata.subscriber_text = bounded_channel_text(text_node(*subscribers), 256);
        }
    }

    channel.identity.title = channel.metadata.title;
    return true;
}

bool channel_blocked_terminal(std::string_view entry) {
    for (const auto key : {
             std::string_view{"reelShelfRenderer"},
             std::string_view{"reelItemRenderer"},
             std::string_view{"shortsLockupViewModel"},
             std::string_view{"adSlotRenderer"},
             std::string_view{"promotedVideoRenderer"},
             std::string_view{"promotedSparklesWebRenderer"},
             std::string_view{"productRenderer"},
             std::string_view{"shoppingShelfRenderer"},
             std::string_view{"merchandiseShelfRenderer"},
             std::string_view{"mealbarPromoRenderer"},
             std::string_view{"premiumUpsellRenderer"}}) {
        if (field(entry, key)) return true;
    }
    return false;
}

bool append_channel_payload(
    core::ChannelPage& channel,
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
            ? "Channel scoped renderer payload was rejected."
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
        channel.results.push_back(result);
        accepted.push_back(std::move(result));
    }

    if (accepted.empty()) return true;
    if (title.size() > kMaxSectionTitleBytes) title.clear();
    if (title.empty() && !channel.sections.empty() && channel.sections.back().title.empty()) {
        auto& existing = channel.sections.back().results;
        existing.insert(existing.end(),
                        std::make_move_iterator(accepted.begin()),
                        std::make_move_iterator(accepted.end()));
    } else {
        core::ChannelSection section;
        section.title = std::move(title);
        section.results = std::move(accepted);
        channel.sections.push_back(std::move(section));
    }
    return true;
}

bool append_channel_entry(
    core::ChannelPage& channel,
    std::unordered_set<std::string>& seen,
    std::string inherited_title,
    std::string_view entry,
    const core::FilterPolicy& policy,
    std::string& continuation,
    bool& ambiguous_continuation,
    bool& saw_reviewed,
    std::string& error) {
    if (channel_blocked_terminal(entry)) {
        saw_reviewed = true;
        return true;
    }

    if (const auto rich_item = field(entry, "richItemRenderer")) {
        saw_reviewed = true;
        const auto content = field(*rich_item, "content");
        return !content || append_channel_entry(
            channel, seen, std::move(inherited_title), *content, policy,
            continuation, ambiguous_continuation, saw_reviewed, error);
    }

    if (const auto rich_section = field(entry, "richSectionRenderer")) {
        saw_reviewed = true;
        const auto content = field(*rich_section, "content");
        if (!content) return true;
        const auto title = section_title(entry);
        if (const auto rich_shelf = field(*content, "richShelfRenderer")) {
            const auto contents = field(*rich_shelf, "contents");
            if (!contents) return true;
            const auto items = array_elements(*contents);
            if (!items) {
                error = "Channel rich-shelf contents are malformed or exceed limits.";
                return false;
            }
            for (const auto item : *items) {
                if (!append_channel_entry(
                        channel, seen, title, item, policy,
                        continuation, ambiguous_continuation, saw_reviewed, error)) return false;
            }
            return true;
        }
        if (const auto shelf = field(*content, "shelfRenderer")) {
            return append_channel_entry(
                channel, seen, title, *shelf, policy,
                continuation, ambiguous_continuation, saw_reviewed, error);
        }
        // reelShelfRenderer and all other unreviewed section payloads are terminal.
        return true;
    }

    if (const auto item_section = field(entry, "itemSectionRenderer")) {
        saw_reviewed = true;
        const auto contents = field(*item_section, "contents");
        if (!contents) return true;
        const auto items = array_elements(*contents);
        if (!items) {
            error = "Channel item-section contents are malformed or exceed limits.";
            return false;
        }
        const auto title = section_title(entry);
        for (const auto item : *items) {
            if (!append_channel_entry(
                    channel, seen, title, item, policy,
                    continuation, ambiguous_continuation, saw_reviewed, error)) return false;
        }
        return true;
    }

    if (const auto rich_shelf = field(entry, "richShelfRenderer")) {
        saw_reviewed = true;
        const auto contents = field(*rich_shelf, "contents");
        if (!contents) return true;
        const auto items = array_elements(*contents);
        if (!items) {
            error = "Channel rich-shelf contents are malformed or exceed limits.";
            return false;
        }
        const auto title = section_title(entry).empty() ? inherited_title : section_title(entry);
        for (const auto item : *items) {
            if (!append_channel_entry(
                    channel, seen, title, item, policy,
                    continuation, ambiguous_continuation, saw_reviewed, error)) return false;
        }
        return true;
    }

    if (const auto shelf = field(entry, "shelfRenderer")) {
        saw_reviewed = true;
        std::optional<std::string_view> items;
        if (const auto content = field(*shelf, "content")) {
            if (const auto horizontal = field(*content, "horizontalListRenderer")) {
                items = field(*horizontal, "items");
            } else if (const auto expanded = field(*content, "expandedShelfContentsRenderer")) {
                items = field(*expanded, "items");
            }
        }
        if (!items) items = field(*shelf, "contents");
        if (!items) return true;
        const auto children = array_elements(*items);
        if (!children) {
            error = "Channel shelf item list is malformed or exceeds limits.";
            return false;
        }
        const auto title = section_title(entry).empty() ? inherited_title : section_title(entry);
        for (const auto child : *children) {
            if (!append_channel_entry(
                    channel, seen, title, child, policy,
                    continuation, ambiguous_continuation, saw_reviewed, error)) return false;
        }
        return true;
    }

    for (const auto key : {
             std::string_view{"videoRenderer"},
             std::string_view{"channelRenderer"},
             std::string_view{"playlistRenderer"},
             std::string_view{"radioRenderer"},
             std::string_view{"lockupViewModel"},
             std::string_view{"continuationItemRenderer"},
             std::string_view{"continuationItemViewModel"},
             std::string_view{"continuationItemView"}}) {
        if (field(entry, key)) {
            saw_reviewed = true;
            return append_channel_payload(
                channel, seen, std::move(inherited_title), entry, policy,
                continuation, ambiguous_continuation, error);
        }
    }

    // Unknown wrapper: deliberately opaque. Do not recurse.
    return true;
}

}  // namespace

ChannelResponseParseResult parse_scoped_channel_response(
    std::string_view response_body,
    std::string_view expected_channel_id,
    const core::FilterPolicy& policy) {
    if (!core::is_valid_channel_id(expected_channel_id)) {
        return channel_fail("Channel request identity is invalid.");
    }
    if (response_body.empty()) return channel_fail("Channel response is empty.");
    if (response_body.size() > kMaxHomeResponseBytes) {
        return channel_fail("Channel response is too large.");
    }
    if (!validate_document(response_body)) {
        return channel_fail("Channel response is malformed or exceeds scope parser limits.");
    }

    const std::string diagnostics = channel_diagnostics(response_body);
    const auto tabs_json = browse_tabs(response_body);
    if (!tabs_json) {
        return channel_fail("Channel response did not contain a recognized browse-tab scope.", diagnostics);
    }
    const auto tabs = array_elements(*tabs_json);
    if (!tabs) return channel_fail("Channel tab list is malformed or exceeds limits.", diagnostics);

    std::optional<std::string_view> selected_contents;
    std::size_t selected_count = 0;
    for (const auto tab : *tabs) {
        const auto renderer = field(tab, "tabRenderer");
        if (!renderer || !tab_selected(*renderer)) continue;
        ++selected_count;
        const auto contents = recognized_tab_contents(*renderer);
        if (contents) selected_contents = *contents;
    }
    if (selected_count != 1) {
        return channel_fail(
            selected_count == 0
                ? "Channel response has no provider-selected browse tab."
                : "Channel response contains multiple selected browse tabs.",
            diagnostics);
    }
    if (!selected_contents) {
        return channel_fail("Selected Channel tab uses an unsupported content container.", diagnostics);
    }

    const auto entries = array_elements(*selected_contents);
    if (!entries) {
        return channel_fail("Selected Channel result container is malformed or exceeds limits.", diagnostics);
    }

    core::ChannelPage channel;
    std::string error;
    if (!apply_channel_metadata(response_body, expected_channel_id, channel, error)) {
        return channel_fail(std::move(error), diagnostics);
    }

    std::unordered_set<std::string> seen;
    seen.reserve(128);
    std::string continuation;
    bool ambiguous_continuation = false;
    bool saw_reviewed = entries->empty();

    for (const auto entry : *entries) {
        if (!append_channel_entry(
                channel,
                seen,
                section_title(entry),
                entry,
                policy,
                continuation,
                ambiguous_continuation,
                saw_reviewed,
                error)) {
            return channel_fail(
                error.empty() ? "Channel selected-tab scope could not be read safely." : std::move(error),
                diagnostics);
        }
    }

    if (!saw_reviewed && !entries->empty()) {
        return channel_fail("Selected Channel tab uses unsupported renderer families.", diagnostics);
    }
    if (!ambiguous_continuation) channel.continuation = std::move(continuation);

    ChannelResponseParseResult result;
    result.page = std::move(channel);
    result.diagnostics = diagnostics;
    return result;
}

}  // namespace ttnx::youtube
