#include "tizentube_nx/core/url.hpp"

#include <algorithm>
#include <cctype>

namespace ttnx::core {
namespace {

bool id_char(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '-' || c == '_';
}

std::string_view strip_fragment(std::string_view value) {
    if (const auto pos = value.find('#'); pos != std::string_view::npos) {
        return value.substr(0, pos);
    }
    return value;
}

std::optional<std::string> query_value(std::string_view query, std::string_view key) {
    std::size_t start = 0;
    while (start <= query.size()) {
        const auto end = query.find('&', start);
        const auto part = query.substr(start, end == std::string_view::npos ? query.size() - start : end - start);
        const auto eq = part.find('=');
        const auto part_key = part.substr(0, eq);
        if (part_key == key && eq != std::string_view::npos) {
            auto value = strip_fragment(part.substr(eq + 1));
            if (is_valid_video_id(value)) {
                return std::string(value);
            }
            return std::nullopt;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return std::nullopt;
}

std::string_view strip_scheme(std::string_view url) {
    if (url.starts_with("https://")) return url.substr(8);
    if (url.starts_with("http://")) return url.substr(7);
    return url;
}

bool host_is(std::string_view host, std::string_view expected) {
    return host == expected || (host.size() > expected.size() + 1 &&
           host.ends_with(expected) && host[host.size() - expected.size() - 1] == '.');
}

}  // namespace

bool is_valid_video_id(std::string_view video_id) {
    return video_id.size() == 11 && std::all_of(video_id.begin(), video_id.end(), id_char);
}

std::optional<std::string> canonical_watch_url(std::string_view video_id) {
    if (!is_valid_video_id(video_id)) return std::nullopt;
    return std::string("https://www.youtube.com/watch?v=") + std::string(video_id);
}

bool is_shorts_url(std::string_view url) {
    const auto no_scheme = strip_scheme(url);
    const auto slash = no_scheme.find('/');
    if (slash == std::string_view::npos) return false;
    const auto host = no_scheme.substr(0, slash);
    if (!host_is(host, "youtube.com")) return false;
    const auto path = no_scheme.substr(slash);
    return path.starts_with("/shorts/") || path == "/shorts";
}

std::optional<std::string> extract_video_id(std::string_view url) {
    if (is_shorts_url(url)) return std::nullopt;

    const auto no_scheme = strip_scheme(url);
    const auto slash = no_scheme.find('/');
    const auto host = slash == std::string_view::npos ? no_scheme : no_scheme.substr(0, slash);
    const auto path_query = slash == std::string_view::npos ? std::string_view{} : no_scheme.substr(slash);

    if (host_is(host, "youtu.be")) {
        auto value = path_query;
        if (value.starts_with('/')) value.remove_prefix(1);
        const auto stop = value.find_first_of("?#/");
        value = value.substr(0, stop);
        if (is_valid_video_id(value)) return std::string(value);
        return std::nullopt;
    }

    if (!host_is(host, "youtube.com")) return std::nullopt;

    if (path_query.starts_with("/watch?")) {
        const auto query = path_query.substr(7);
        return query_value(query, "v");
    }

    if (path_query.starts_with("/embed/") || path_query.starts_with("/live/")) {
        const auto prefix = path_query.starts_with("/embed/") ? std::string_view("/embed/") : std::string_view("/live/");
        auto value = path_query.substr(prefix.size());
        const auto stop = value.find_first_of("?#/");
        value = value.substr(0, stop);
        if (is_valid_video_id(value)) return std::string(value);
    }

    return std::nullopt;
}

std::optional<std::string> canonicalize_youtube_video_url(std::string_view url) {
    const auto id = extract_video_id(url);
    if (!id) return std::nullopt;
    return canonical_watch_url(*id);
}

}  // namespace ttnx::core
