#include "tizentube_nx/core/search_model.hpp"

#include <unordered_set>
#include <utility>

namespace ttnx::core {

std::uint64_t SearchModel::begin_query(std::string query) {
    ++generation_;
    query_ = std::move(query);
    results_.clear();
    continuation_.clear();
    selected_identity_.clear();
    error_.clear();
    state_ = query_.empty() ? SearchViewState::Idle : SearchViewState::Loading;
    return generation_;
}

bool SearchModel::apply_first_page(std::uint64_t generation, const BrowsePage& page) {
    if (!generation_matches(generation) || state_ != SearchViewState::Loading) return false;

    results_ = page.results;
    continuation_ = page.continuation;
    error_.clear();
    selected_identity_.clear();
    state_ = results_.empty() ? SearchViewState::Empty : SearchViewState::Ready;
    return true;
}

bool SearchModel::begin_load_more(std::uint64_t generation) {
    if (!generation_matches(generation) || continuation_.empty()) return false;
    if (state_ != SearchViewState::Ready && state_ != SearchViewState::Empty &&
        state_ != SearchViewState::Error) {
        return false;
    }
    error_.clear();
    state_ = SearchViewState::LoadingMore;
    return true;
}

bool SearchModel::apply_continuation(std::uint64_t generation, const BrowsePage& page) {
    if (!generation_matches(generation) || state_ != SearchViewState::LoadingMore) return false;

    std::unordered_set<std::string> seen;
    seen.reserve(results_.size() + page.results.size());
    for (const auto& result : results_) {
        const auto identity = browse_result_identity(result);
        if (!identity.empty()) seen.insert(identity);
    }
    for (const auto& result : page.results) {
        const auto identity = browse_result_identity(result);
        if (!identity.empty() && seen.insert(identity).second) results_.push_back(result);
    }

    continuation_ = page.continuation;
    error_.clear();
    if (!selected_identity_.empty() && !contains_identity(selected_identity_)) {
        selected_identity_.clear();
    }
    state_ = results_.empty() ? SearchViewState::Empty : SearchViewState::Ready;
    return true;
}

bool SearchModel::fail(std::uint64_t generation, std::string error) {
    if (!generation_matches(generation) ||
        (state_ != SearchViewState::Loading && state_ != SearchViewState::LoadingMore)) {
        return false;
    }
    error_ = std::move(error);
    state_ = SearchViewState::Error;
    return true;
}

bool SearchModel::select(std::string_view identity) {
    if (identity.empty() || !contains_identity(identity)) return false;
    selected_identity_ = std::string(identity);
    return true;
}

void SearchModel::clear_selection() {
    selected_identity_.clear();
}

void SearchModel::reset() {
    ++generation_;
    state_ = SearchViewState::Idle;
    query_.clear();
    results_.clear();
    continuation_.clear();
    selected_identity_.clear();
    error_.clear();
}

bool SearchModel::generation_matches(std::uint64_t generation) const noexcept {
    return generation != 0 && generation == generation_;
}

bool SearchModel::contains_identity(std::string_view identity) const {
    for (const auto& result : results_) {
        if (browse_result_identity(result) == identity) return true;
    }
    return false;
}

}  // namespace ttnx::core
