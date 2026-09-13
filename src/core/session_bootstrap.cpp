#include "tizentube_nx/youtube/session_bootstrap.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ttnx::youtube {
namespace {

constexpr std::size_t kMaxBootstrapBytes = 4 * 1024 * 1024;
constexpr unsigned kMaxJsonDepth = 128;

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
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

class JsonCursor {
public:
    explicit JsonCursor(std::string_view input) : input_(input) {}

    void skip_ws() {
        while (pos_ < input_.size() && is_ws(input_[pos_])) ++pos_;
    }

    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

    bool skip_value(unsigned depth = 0) {
        if (depth > kMaxJsonDepth) return false;
        skip_ws();
        if (pos_ >= input_.size()) return false;

        switch (input_[pos_]) {
            case '"': return skip_string();
            case '[': return skip_array(depth + 1);
            case '{': return skip_object(depth + 1);
            case 't': return skip_literal("true");
            case 'f': return skip_literal("false");
            case 'n': return skip_literal("null");
            default: return skip_number();
        }
    }

private:
    std::string_view input_;
    std::size_t pos_{0};

    bool skip_literal(std::string_view literal) {
        if (input_.substr(pos_, literal.size()) != literal) return false;
        pos_ += literal.size();
        return true;
    }

    bool skip_string() {
        if (pos_ >= input_.size() || input_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') continue;
            if (pos_ >= input_.size()) return false;
            const char escape = input_[pos_++];
            switch (escape) {
                case '"': case '\\': case '/': case 'b': case 'f':
                case 'n': case 'r': case 't':
                    break;
                case 'u':
                    if (pos_ + 4 > input_.size()) return false;
                    for (std::size_t i = 0; i < 4; ++i) {
                        if (!is_hex(input_[pos_ + i])) return false;
                    }
                    pos_ += 4;
                    break;
                default:
                    return false;
            }
        }
        return false;
    }

    bool skip_number() {
        const std::size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        if (pos_ >= input_.size()) return false;

        if (input_[pos_] == '0') {
            ++pos_;
        } else if (input_[pos_] >= '1' && input_[pos_] <= '9') {
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        } else {
            return false;
        }

        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            const std::size_t fractional_start = pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (fractional_start == pos_) return false;
        }

        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            const std::size_t exponent_start = pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (exponent_start == pos_) return false;
        }
        return pos_ > start;
    }

    bool skip_array(unsigned depth) {
        if (depth > kMaxJsonDepth || input_[pos_] != '[') return false;
        ++pos_;
        skip_ws();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            ++pos_;
            return true;
        }

        while (pos_ < input_.size()) {
            if (!skip_value(depth)) return false;
            skip_ws();
            if (pos_ >= input_.size()) return false;
            if (input_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (input_[pos_] != ',') return false;
            ++pos_;
            skip_ws();
        }
        return false;
    }

    bool skip_object(unsigned depth) {
        if (depth > kMaxJsonDepth || input_[pos_] != '{') return false;
        ++pos_;
        skip_ws();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            ++pos_;
            return true;
        }

        while (pos_ < input_.size()) {
            if (!skip_string()) return false;
            skip_ws();
            if (pos_ >= input_.size() || input_[pos_] != ':') return false;
            ++pos_;
            if (!skip_value(depth)) return false;
            skip_ws();
            if (pos_ >= input_.size()) return false;
            if (input_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (input_[pos_] != ',') return false;
            ++pos_;
            skip_ws();
        }
        return false;
    }
};

std::optional<std::string_view> array_element(std::string_view array_json, std::size_t wanted) {
    std::size_t pos = 0;
    while (pos < array_json.size() && is_ws(array_json[pos])) ++pos;
    if (pos >= array_json.size() || array_json[pos] != '[') return std::nullopt;
    ++pos;

    std::size_t index = 0;
    while (true) {
        while (pos < array_json.size() && is_ws(array_json[pos])) ++pos;
        if (pos >= array_json.size() || array_json[pos] == ']') return std::nullopt;

        JsonCursor cursor(array_json.substr(pos));
        if (!cursor.skip_value()) return std::nullopt;
        const std::size_t length = cursor.position();
        if (length == 0 || pos + length > array_json.size()) return std::nullopt;
        if (index == wanted) return array_json.substr(pos, length);

        pos += length;
        while (pos < array_json.size() && is_ws(array_json[pos])) ++pos;
        if (pos >= array_json.size() || array_json[pos] != ',') return std::nullopt;
        ++pos;
        ++index;
    }
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

std::optional<std::uint32_t> read_u16_escape(std::string_view json, std::size_t& pos) {
    if (pos + 4 > json.size()) return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!is_hex(json[pos + i])) return std::nullopt;
        value = (value << 4) | hex_value(json[pos + i]);
    }
    pos += 4;
    return value;
}

std::optional<std::string> decode_json_string(std::string_view json) {
    std::size_t pos = 0;
    while (pos < json.size() && is_ws(json[pos])) ++pos;
    if (pos >= json.size() || json[pos++] != '"') return std::nullopt;

    std::string out;
    out.reserve(json.size());
    while (pos < json.size()) {
        const unsigned char c = static_cast<unsigned char>(json[pos++]);
        if (c == '"') {
            while (pos < json.size() && is_ws(json[pos])) ++pos;
            return pos == json.size() ? std::optional<std::string>(std::move(out)) : std::nullopt;
        }
        if (c < 0x20) return std::nullopt;
        if (c != '\\') {
            out.push_back(static_cast<char>(c));
            continue;
        }

        if (pos >= json.size()) return std::nullopt;
        switch (json[pos++]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto first = read_u16_escape(json, pos);
                if (!first) return std::nullopt;
                std::uint32_t codepoint = *first;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (pos + 2 > json.size() || json[pos] != '\\' || json[pos + 1] != 'u') return std::nullopt;
                    pos += 2;
                    auto second = read_u16_escape(json, pos);
                    if (!second || *second < 0xdc00 || *second > 0xdfff) return std::nullopt;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*second - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return std::nullopt;
                }
                if (!append_utf8(out, codepoint)) return std::nullopt;
                break;
            }
            default:
                return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> optional_string_field(std::string_view array_json, std::size_t index) {
    const auto element = array_element(array_json, index);
    if (!element) return std::nullopt;
    return decode_json_string(*element);
}

SessionBootstrapParseResult fail(std::string message) {
    SessionBootstrapParseResult result;
    result.error = std::move(message);
    return result;
}

}  // namespace

SessionBootstrapParseResult parse_session_bootstrap(
    std::string_view response_body,
    const SessionBootstrapOptions& options) {
    if (response_body.size() > kMaxBootstrapBytes) {
        return fail("Session bootstrap response is too large.");
    }
    if (!response_body.starts_with(")]}'")) {
        return fail("Session bootstrap response is missing the JSPB prefix.");
    }

    std::string_view json = response_body.substr(4);
    while (!json.empty() && is_ws(json.front())) json.remove_prefix(1);
    if (json.empty()) return fail("Session bootstrap response is empty.");

    // Current YouTube web bootstrap shape:
    // data[0][2] -> ytcfg, ytcfg[0][0] -> device_info.
    const auto root0 = array_element(json, 0);
    if (!root0) return fail("Session bootstrap root shape is unsupported.");
    const auto ytcfg = array_element(*root0, 2);
    if (!ytcfg) return fail("Session bootstrap config shape is unsupported.");
    const auto device_group = array_element(*ytcfg, 0);
    if (!device_group) return fail("Session bootstrap device group is missing.");
    const auto device_info = array_element(*device_group, 0);
    if (!device_info) return fail("Session bootstrap device info is missing.");

    const auto visitor_data = optional_string_field(*device_info, 13);
    const auto client_version = optional_string_field(*device_info, 16);
    if (!visitor_data || visitor_data->empty()) {
        return fail("Session bootstrap visitor data is missing or invalid.");
    }
    if (!client_version || client_version->empty()) {
        return fail("Session bootstrap client version is missing or invalid.");
    }

    GuestSession session;
    session.api_version = "v1";
    session.client_name = "WEB";
    session.client_version = *client_version;
    session.visitor_data = *visitor_data;
    session.user_agent = options.user_agent;

    if (!options.language.empty()) {
        session.language = options.language;
    } else if (const auto language = optional_string_field(*device_info, 0); language && !language->empty()) {
        session.language = *language;
    }

    if (!options.region.empty()) {
        session.region = options.region;
    } else if (const auto region = optional_string_field(*device_info, 1); region && !region->empty()) {
        session.region = *region;
    }

    if (!options.timezone.empty()) {
        session.timezone = options.timezone;
    } else if (const auto timezone = optional_string_field(*device_info, 79); timezone && !timezone->empty()) {
        session.timezone = *timezone;
    }

    if (!session.usable()) return fail("Session bootstrap did not produce a usable guest session.");

    SessionBootstrapParseResult result;
    result.session = std::move(session);
    return result;
}

}  // namespace ttnx::youtube
