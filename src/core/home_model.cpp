#include "tizentube_nx/core/home_model.hpp"

#include "tizentube_nx/core/search_result.hpp"

#include <algorithm>
#include <utility>

namespace ttnx::core {
namespace {

constexpr std::size_t kMaxHomeErrorBytes = 512;

std::string bounded_error(std::string error) {
    if (error.size() > kMaxHomeErrorBytes) error.resize(kMaxHomeErrorBytes);
    return error;
}

}  // namespace

std::uint64_t HomeModel::begin_load() {
    ++generation_;
    state_ = HomeViewState::Loading;
    error_.clear();
    return generation_;
}

bool HomeModel::apply_page(std::uint64_t generation, HomePage page) {
    if (generation != generation_ || state_ != HomeViewState::Loading) return false;

    // A real normalized result set wins over any auxiliary empty-state marker.
    if (!page.results.empty()) page.empty_reason = HomeEmptyReason::None;
    page_ = std::move(page);
    state_ = page_.results.empty() ? HomeViewState::Empty : HomeViewState::Ready;
    error_.clear();

    if (!selected_identity_.empty() && !contains_identity(selected_identity_)) {
        selected_identity_.clear();
    }
    return true;
}

bool HomeModel::fail(std::uint64_t generation, std::string error) {
    if (generation != generation_ || state_ != HomeViewState::Loading) return false;
    state_ = HomeViewState::Error;
    error_ = bounded_error(std::move(error));
    return true;
}

void HomeModel::reset() {
    ++generation_;
    state_ = HomeViewState::Idle;
    page_ = {};
    selected_identity_.clear();
    error_.clear();
}

bool HomeModel::select(std::string_view identity) {
    if (identity.empty() || !contains_identity(identity)) return false;
    selected_identity_ = std::string(identity);
    return true;
}

void HomeModel::clear_selection() {
    selected_identity_.clear();
}

bool HomeModel::contains_identity(std::string_view identity) const {
    return std::any_of(page_.results.begin(), page_.results.end(), [&](const BrowseResult& result) {
        return browse_result_identity(result) == identity;
    });
}

}  // namespace ttnx::core
