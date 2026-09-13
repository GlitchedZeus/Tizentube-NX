#pragma once

#include "tizentube_nx/core/guest_browse.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::core {

enum class SearchViewState {
    Idle,
    Loading,
    Ready,
    Empty,
    Error,
    LoadingMore,
};

// Pure UI-facing Search state. It owns no transport and cannot initiate network
// traffic. A future Switch controller may feed it already-sanitized BrowsePage
// values only after the real-device networking gate is accepted.
class SearchModel {
public:
    // Starts a new logical query and invalidates any older async completion.
    // Returns a monotonically increasing generation token for result callbacks.
    [[nodiscard]] std::uint64_t begin_query(std::string query);

    [[nodiscard]] bool apply_first_page(std::uint64_t generation, const BrowsePage& page);
    [[nodiscard]] bool begin_load_more(std::uint64_t generation);
    [[nodiscard]] bool apply_continuation(std::uint64_t generation, const BrowsePage& page);
    [[nodiscard]] bool fail(std::uint64_t generation, std::string error);

    [[nodiscard]] bool select(std::string_view identity);
    void clear_selection();
    void reset();

    [[nodiscard]] SearchViewState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    [[nodiscard]] const std::vector<BrowseResult>& results() const noexcept { return results_; }
    [[nodiscard]] const std::string& continuation() const noexcept { return continuation_; }
    [[nodiscard]] const std::string& selected_identity() const noexcept { return selected_identity_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] bool can_load_more() const noexcept {
        return !continuation_.empty() && state_ != SearchViewState::Loading &&
               state_ != SearchViewState::LoadingMore;
    }

private:
    bool generation_matches(std::uint64_t generation) const noexcept;
    bool contains_identity(std::string_view identity) const;

    SearchViewState state_{SearchViewState::Idle};
    std::uint64_t generation_{0};
    std::string query_;
    std::vector<BrowseResult> results_;
    std::string continuation_;
    std::string selected_identity_;
    std::string error_;
};

}  // namespace ttnx::core
