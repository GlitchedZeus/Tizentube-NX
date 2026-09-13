#pragma once

#include "tizentube_nx/core/settings.hpp"

namespace ttnx {

inline constexpr auto kAppDirectory = "sdmc:/switch/TizenTube-NX";

// Boot records contain fixed event labels only, never user input or secrets.
bool initialize_storage();
core::Settings load_settings();
bool save_settings(const core::Settings& settings);
void record_boot_event(const char* event);

} // namespace ttnx
