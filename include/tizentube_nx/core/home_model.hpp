#pragma once

#include "tizentube_nx/core/search_result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::core {

struct HomeSection {
    std::string title;
    std::vector<BrowseResult> results;
};

struct HomePage {
    std::vector<HomeSection> sections;
    std::vector<BrowseResult> results;
    std::string continuation;

    [[nodiscard]] bool has_more() const noexcept {
        return !continuation.empty();
    }
};

enum class HomeViewState {
    Idle,
    Loading,
    Ready,
    Empty,
    Error,
};

// UI-thread-owned normalized Home state. Worker threads publish only a HomePage
// plus the generation captured when the explicit Load/Refresh action started.
// Borealis Views are intentionally not stored here so TabFrame destruction can
// never invalidate model state.
class HomeModel {
public:
    [[nodiscard]] std::uint64_t begin_load();
    [[nodiscard]] bool apply_page(std::uint64_t generation, HomePage page);
    [[nodiscard]] bool fail(std::uint64_t generation, std::string error);
    void reset();

    [[nodiscard]] bool select(std::string_view identity);
    void clear_selection();

    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] HomeViewState state() const noexcept { return state_; }
    [[nodiscard]] const HomePage& page() const noexcept { return page_; }
    [[nodiscard]] const std::vector<BrowseResult>& results() const noexcept { return page_.results; }
    [[nodiscard]] const std::vector<HomeSection>& sections() const noexcept { return page_.sections; }
    [[nodiscard]] const std::string& selected_identity() const noexcept { return selected_identity_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    [[nodiscard]] bool contains_identity(std::string_view identity) const;

    std::uint64_t generation_{0};
    HomeViewState state_{HomeViewState::Idle};
    HomePage page_;
    std::string selected_identity_;
    std::string error_;
};

}  // namespace ttnx::core
