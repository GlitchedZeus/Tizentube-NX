#include "tizentube_nx/core/url.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace ttnx::core {
namespace {

constexpr std::size_t kMaxInputUrlBytes = 4096;
constexpr std::size_t kMaxPlaylistIdBytes = 128;

bool id_char(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '-' || c == '_';
}

bool all_id_chars(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), id_char);
}

bool has_invalid_url_char(std::string_view value) {
    for (const unsigned char c : value) {
        if (c <= 0x20 || c == 0x7f) return true;
    }
    return false;
}

std::string lowercase_ascii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool is_youtube_host(std::string_view host) {
    const auto normalized = lowercase_ascii(host);
    return normalized == "youtube.com" || normalized == "www.youtube.com" ||
           normalized == "m.youtube.com" || normalized == "music.youtube.com";
}

bool is_short_host(std::string_view host) {
    const auto normalized = lowercase_ascii(host);
    return normalized == "youtu.be" || normalized == "www.youtu.be";
}

struct ParsedUrl {
    std::string host;
    std::string_view path;
    std::string_view query;
};

std::optional<ParsedUrl> parse_basic_url(std::string_view url) {
    if (url.empty() || url.size() > kMaxInputUrlBytes || has_invalid_url_char(url)) {
        return std::nullopt;
    }

    std::size_t scheme_size = 0;
    if (url.starts_with("https://")) {
        scheme_size = 8;
    } else if (url.starts_with("http://")) {
        // Input normalization may accept an old HTTP link, but canonical output
        // is always HTTPS and this helper performs no networking.
        scheme_size = 7;
    } else {
        return std::nullopt;
    }

    const auto rest = url.substr(scheme_size);
    const auto authority_end = rest.find_first_of("/?#");
    const auto authority = rest.substr(
        0, authority_end == std::string_view::npos ? rest.size() : authority_end);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find(':') != std::string_view::npos ||
        authority.find('[') != std::string_view::npos ||
        authority.find(']') != std::string_view::npos) {
        return std::nullopt;
    }

    const auto host = lowercase_ascii(authority);
    if (!is_youtube_host(host) && !is_short_host(host)) return std::nullopt;

    std::string_view remainder;
    if (authority_end != std::string_view::npos) remainder = rest.substr(authority_end);
    if (const auto fragment = remainder.find('#'); fragment != std::string_view::npos) {
        remainder = remainder.substr(0, fragment);
    }

    std::string_view path = remainder;
    std::string_view query;
    if (const auto question = remainder.find('?'); question != std::string_view::npos) {
        path = remainder.substr(0, question);
        query = remainder.substr(question + 1);
    }
    if (path.empty()) path = "/";

    return ParsedUrl{host, path, query};
}

std::optional<std::string> single_video_query_value(std::string_view query) {
    bool seen = false;
    std::optional<std::string> value;
    std::size_t start = 0;

    while (start <= query.size()) {
        const auto end = query.find('&', start);
        const auto part = query.substr(
            start, end == std::string_view::npos ? query.size() - start : end - start);
        const auto eq = part.find('=');
        const auto key = part.substr(0, eq);

        if (key == "v") {
            if (seen || eq == std::string_view::npos) return std::nullopt;
            seen = true;
            const auto candidate = part.substr(eq + 1);
            if (!is_valid_video_id(candidate)) return std::nullopt;
            value = std::string(candidate);
        }

        if (end == std::string_view::npos) break;
        start = end + 1;
    }

    return value;
}

std::optional<std::string> path_video_id(
    std::string_view path,
    std::string_view prefix) {
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto candidate = path.substr(prefix.size());
    if (candidate.find('/') != std::string_view::npos || !is_valid_video_id(candidate)) {
        return std::nullopt;
    }
    return std::string(candidate);
}

bool path_is_shorts_or_reel(std::string_view path) {
    return path == "/shorts" || path.starts_with("/shorts/") ||
           path == "/reel" || path.starts_with("/reel/");
}

}  // namespace

bool is_valid_video_id(std::string_view video_id) {
    return video_id.size() == 11 && all_id_chars(video_id);
}

bool is_valid_channel_id(std::string_view channel_id) {
    return channel_id.size() == 24 && channel_id.starts_with("UC") && all_id_chars(channel_id);
}

bool is_valid_playlist_id(std::string_view playlist_id) {
    return playlist_id.size() >= 2 && playlist_id.size() <= kMaxPlaylistIdBytes &&
           all_id_chars(playlist_id);
}

std::optional<std::string> canonical_watch_url(std::string_view video_id) {
    if (!is_valid_video_id(video_id)) return std::nullopt;
    return std::string("https://www.youtube.com/watch?v=") + std::string(video_id);
}

std::optional<std::string> canonical_channel_url(std::string_view channel_id) {
    if (!is_valid_channel_id(channel_id)) return std::nullopt;
    return std::string("https://www.youtube.com/channel/") + std::string(channel_id);
}

std::optional<std::string> canonical_playlist_url(std::string_view playlist_id) {
    if (!is_valid_playlist_id(playlist_id)) return std::nullopt;
    return std::string("https://www.youtube.com/playlist?list=") + std::string(playlist_id);
}

bool is_shorts_url(std::string_view url) {
    const auto parsed = parse_basic_url(url);
    return parsed && is_youtube_host(parsed->host) && path_is_shorts_or_reel(parsed->path);
}

std::optional<std::string> extract_video_id(std::string_view url) {
    const auto parsed = parse_basic_url(url);
    if (!parsed) return std::nullopt;

    if (is_youtube_host(parsed->host) && path_is_shorts_or_reel(parsed->path)) {
        return std::nullopt;
    }

    if (is_short_host(parsed->host)) {
        if (!parsed->path.starts_with('/')) return std::nullopt;
        const auto candidate = parsed->path.substr(1);
        if (candidate.find('/') != std::string_view::npos || !is_valid_video_id(candidate)) {
            return std::nullopt;
        }
        return std::string(candidate);
    }

    if (parsed->path == "/watch") {
        return single_video_query_value(parsed->query);
    }
    if (const auto embed = path_video_id(parsed->path, "/embed/")) return embed;
    if (const auto live = path_video_id(parsed->path, "/live/")) return live;

    return std::nullopt;
}

std::optional<std::string> canonicalize_youtube_video_url(std::string_view url) {
    const auto id = extract_video_id(url);
    return id ? canonical_watch_url(*id) : std::nullopt;
}

}  // namespace ttnx::core
