#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ttnx::core {

// UI preferences only. Content exclusions are never user-configurable here.
struct Settings {
    bool show_fps = false;
};

std::optional<Settings> parse_settings(std::string_view text);
std::string serialize_settings(const Settings& settings);

} // namespace ttnx::core
