#pragma once

#include <array>
#include <string_view>

namespace ttnx::ui {

enum class RootSection {
    Home,
    Search,
    Subscriptions,
    Library,
    Settings,
};

struct RootNavItem {
    RootSection section;
    std::string_view label;
};

inline constexpr std::array<RootNavItem, 5> kRootNavigation {{
    {RootSection::Home, "Home"},
    {RootSection::Search, "Search"},
    {RootSection::Subscriptions, "Subscriptions"},
    {RootSection::Library, "Library"},
    {RootSection::Settings, "Settings"},
}};

constexpr bool root_navigation_contains(std::string_view label) {
    for (const auto& item : kRootNavigation) {
        if (item.label == label) {
            return true;
        }
    }
    return false;
}

static_assert(kRootNavigation.size() == 5);
static_assert(!root_navigation_contains("Shorts"), "Shorts must never be a root navigation destination");

}  // namespace ttnx::ui
