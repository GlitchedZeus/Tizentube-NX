#include "tizentube_nx/net/http1.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>

namespace ttnx::net {
namespace {

constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxRawOverheadBytes = 1024 * 1024;

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool contains_crlf(std::string_view value) {
    return value.find('\r') != std::string_view::npos ||
           value.find('\n') != std::string_view::npos;
}

bool valid_header_name(std::string_view name) {
    if (name.empty()) return false;
    for (const unsigned char c : name) {
        if (std::isalnum(c)) continue;
        switch (c) {
            case '!': case '#': case '$': case '%': case '&': case '\'':
            case '*': case '+': case '-': case '.': case '^': case '_':
            case '`': case '|': case '~':
                continue;
            default:
                return false;
        }
    }
    return true;
}

bool valid_host_ascii(std::string_view host) {
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') {
        return false;
    }
    for (const unsigned char c : host) {
        if (!(std::isalnum(c) || c == '-' || c == '.')) return false;
    }
    return true;
}

Http1ParseResult fail(std::string message) {
    Http1ParseResult result;
    result.error = std::move(message);
    return result;
}

std::optional<std::size_t> parse_decimal_size(std::string_view value) {
    value = trim(value);
    if (value.empty()) return std::nullopt;
    std::size_t parsed = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != last) return std::nullopt;
    return parsed;
}

std::optional<std::size_t> parse_hex_size(std::string_view value) {
    const auto semicolon = value.find(';');
    value = trim(value.substr(0, semicolon));
    if (value.empty()) return std::nullopt;
    std::size_t parsed = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, parsed, 16);
    if (result.ec != std::errc{} || result.ptr != last) return std::nullopt;
    return parsed;
}

std::optional<std::string> decode_chunked(
    std::string_view encoded,
    std::size_t max_body_bytes) {
    std::string decoded;
    std::size_t pos = 0;

    while (true) {
        const auto line_end = encoded.find("\r\n", pos);
        if (line_end == std::string_view::npos) return std::nullopt;
        const auto chunk_size = parse_hex_size(encoded.substr(pos, line_end - pos));
        if (!chunk_size) return std::nullopt;
        pos = line_end + 2;

        if (*chunk_size == 0) {
            if (pos + 2 <= encoded.size() && encoded.substr(pos, 2) == "\r\n") {
                return decoded;
            }
            const auto trailer_end = encoded.find("\r\n\r\n", pos);
            if (trailer_end == std::string_view::npos) return std::nullopt;
            return decoded;
        }

        if (*chunk_size > max_body_bytes - decoded.size()) return std::nullopt;
        if (pos > encoded.size() || *chunk_size > encoded.size() - pos) return std::nullopt;

        decoded.append(encoded.substr(pos, *chunk_size));
        pos += *chunk_size;
        if (pos + 2 > encoded.size() || encoded.substr(pos, 2) != "\r\n") {
            return std::nullopt;
        }
        pos += 2;
    }
}

}  // namespace

std::optional<HttpsUrl> parse_https_url(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size()) return std::nullopt;
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(url[i])) != scheme[i]) {
            return std::nullopt;
        }
    }

    const auto authority_start = scheme.size();
    const auto authority_end = url.find_first_of("/?#", authority_start);
    const auto authority = url.substr(
        authority_start,
        authority_end == std::string_view::npos ? std::string_view::npos
                                                : authority_end - authority_start);
    if (authority.empty() || authority.find('@') != std::string_view::npos) return std::nullopt;
    if (authority.front() == '[') return std::nullopt;

    std::string_view host = authority;
    unsigned short port = 443;
    if (const auto colon = authority.rfind(':'); colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        const auto port_text = authority.substr(colon + 1);
        if (port_text.empty()) return std::nullopt;
        unsigned long value = 0;
        for (const char c : port_text) {
            if (c < '0' || c > '9') return std::nullopt;
            value = value * 10 + static_cast<unsigned long>(c - '0');
            if (value > std::numeric_limits<unsigned short>::max()) return std::nullopt;
        }
        if (value == 0) return std::nullopt;
        port = static_cast<unsigned short>(value);
    }

    if (!valid_host_ascii(host)) return std::nullopt;

    std::string normalized_host(host);
    std::transform(normalized_host.begin(), normalized_host.end(), normalized_host.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string target = "/";
    if (authority_end != std::string_view::npos) {
        const auto fragment = url.find('#', authority_end);
        const auto end = fragment == std::string_view::npos ? url.size() : fragment;
        if (url[authority_end] == '/') {
            target.assign(url.substr(authority_end, end - authority_end));
        } else if (url[authority_end] == '?') {
            target = "/";
            target.append(url.substr(authority_end, end - authority_end));
        }
    }
    if (target.empty() || target.front() != '/' || contains_crlf(target)) return std::nullopt;

    return HttpsUrl{std::move(normalized_host), std::move(target), port};
}

std::optional<std::string> build_http1_request(
    const HttpRequest& request,
    const HttpsUrl& url) {
    if (!valid_host_ascii(url.host) || url.target.empty() || url.target.front() != '/' ||
        contains_crlf(url.target)) {
        return std::nullopt;
    }

    std::string output;
    output.reserve(512 + request.body.size());
    output += request.method == HttpMethod::Post ? "POST " : "GET ";
    output += url.target;
    output += " HTTP/1.1\r\nHost: ";
    output += url.host;
    if (url.port != 443) {
        output += ':';
        output += std::to_string(url.port);
    }
    output += "\r\nConnection: close\r\nAccept-Encoding: identity\r\n";

    for (const auto& header : request.headers) {
        if (!valid_header_name(header.name) || contains_crlf(header.value)) return std::nullopt;
        if (iequals(header.name, "Host") ||
            iequals(header.name, "Connection") ||
            iequals(header.name, "Content-Length") ||
            iequals(header.name, "Transfer-Encoding") ||
            iequals(header.name, "Accept-Encoding")) {
            continue;
        }
        output += header.name;
        output += ": ";
        output += header.value;
        output += "\r\n";
    }

    if (request.method == HttpMethod::Post) {
        output += "Content-Length: ";
        output += std::to_string(request.body.size());
        output += "\r\n";
    }

    output += "\r\n";
    if (request.method == HttpMethod::Post) output += request.body;
    return output;
}

Http1ParseResult parse_http1_response(
    std::string_view raw_response,
    std::size_t max_body_bytes) {
    if (raw_response.size() > max_body_bytes + kMaxRawOverheadBytes) {
        return fail("HTTP response exceeds the raw response limit.");
    }

    const auto header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string_view::npos || header_end > kMaxHeaderBytes) {
        return fail("HTTP response headers are missing or too large.");
    }

    const auto status_end = raw_response.find("\r\n");
    if (status_end == std::string_view::npos || status_end > header_end) {
        return fail("HTTP status line is malformed.");
    }

    const auto status_line = raw_response.substr(0, status_end);
    if (!(status_line.starts_with("HTTP/1.1 ") || status_line.starts_with("HTTP/1.0 "))) {
        return fail("Unsupported HTTP status line.");
    }
    if (status_line.size() < 12) return fail("HTTP status code is missing.");

    int status_code = 0;
    const auto code_text = status_line.substr(9, 3);
    const auto code_result = std::from_chars(
        code_text.data(), code_text.data() + code_text.size(), status_code, 10);
    if (code_result.ec != std::errc{} || code_result.ptr != code_text.data() + code_text.size() ||
        status_code < 100 || status_code > 599) {
        return fail("HTTP status code is invalid.");
    }

    HttpResponse response;
    response.status_code = status_code;

    std::size_t line_start = status_end + 2;
    while (line_start < header_end) {
        const auto line_end = raw_response.find("\r\n", line_start);
        if (line_end == std::string_view::npos || line_end > header_end) {
            return fail("HTTP header line is malformed.");
        }
        const auto line = raw_response.substr(line_start, line_end - line_start);
        if (line.empty()) break;
        if (line.front() == ' ' || line.front() == '\t') {
            return fail("Folded HTTP headers are not supported.");
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) return fail("HTTP header is missing a colon.");
        const auto name = line.substr(0, colon);
        const auto value = trim(line.substr(colon + 1));
        if (!valid_header_name(name) || contains_crlf(value)) {
            return fail("HTTP header contains invalid characters.");
        }
        response.headers.push_back({std::string(name), std::string(value)});
        line_start = line_end + 2;
    }

    const std::string_view encoded_body = raw_response.substr(header_end + 4);
    const auto transfer_encoding = find_header_value(response, "Transfer-Encoding");
    const auto content_length = find_header_value(response, "Content-Length");

    if (transfer_encoding) {
        if (!iequals(trim(*transfer_encoding), "chunked")) {
            return fail("Unsupported HTTP Transfer-Encoding.");
        }
        if (content_length) {
            return fail("Ambiguous HTTP response has both chunked encoding and Content-Length.");
        }
        auto decoded = decode_chunked(encoded_body, max_body_bytes);
        if (!decoded) return fail("Chunked HTTP body is malformed or too large.");
        response.body = std::move(*decoded);
    } else if (content_length) {
        const auto length = parse_decimal_size(*content_length);
        if (!length || *length > max_body_bytes) {
            return fail("HTTP Content-Length is invalid or too large.");
        }
        if (encoded_body.size() < *length) {
            return fail("HTTP body is shorter than Content-Length.");
        }
        response.body.assign(encoded_body.substr(0, *length));
    } else {
        if (encoded_body.size() > max_body_bytes) return fail("HTTP body is too large.");
        response.body.assign(encoded_body);
    }

    Http1ParseResult result;
    result.response = std::move(response);
    return result;
}

std::optional<std::string_view> find_header_value(
    const HttpResponse& response,
    std::string_view name) {
    for (const auto& header : response.headers) {
        if (iequals(header.name, name)) return header.value;
    }
    return std::nullopt;
}

}  // namespace ttnx::net
