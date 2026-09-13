#include "tizentube_nx/core/settings.hpp"

namespace ttnx::core {

std::optional<Settings> parse_settings(std::string_view text) {
    if (text.size() > 512) return std::nullopt;
    Settings result;
    bool version_seen = false;
    bool fps_seen = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (line == "version=1" && !version_seen) {
            version_seen = true;
        } else if ((line == "show_fps=0" || line == "show_fps=1") && !fps_seen) {
            result.show_fps = line.back() == '1';
            fps_seen = true;
        } else {
            return std::nullopt;
        }
    }
    if (!version_seen || !fps_seen) return std::nullopt;
    return result;
}

std::string serialize_settings(const Settings& settings) {
    return std::string("version=1\nshow_fps=") + (settings.show_fps ? "1\n" : "0\n");
}

} // namespace ttnx::core
