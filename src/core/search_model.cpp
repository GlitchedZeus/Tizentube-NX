#include "tizentube_nx/core/search_model.hpp"

#include <unordered_set>
#include <utility>

namespace ttnx::core {
namespace {

bool retryable_code(SearchErrorCode code) {
    return code == SearchErrorCode::NetworkUnavailable ||
           code == SearchErrorCode::HttpFailure ||
           code == SearchErrorCode::ContinuationFailure;
}

}  // namespace

std::uint64_t SearchModel::begin_query(std::string query) {
    ++generation_;
    query_ = std::move(query);
    results_.clear();
    continuation_.clear();
    selected_identity_.clear();
    clear_problem();
    failed_while_loading_more_ = false;
    end_of_results_ = false;
    state_ = query_.empty() ? SearchViewState::Idle : SearchViewState::Loading;
    return generation_;
}

bool SearchModel::apply_first_page(std::uint64_t generation, const BrowsePage& page) {
    if (!generation_matches(generation) || state_ != SearchViewState::Loading) return false;

    results_ = page.results;
    continuation_ = page.continuation;
    selected_identity_.clear();
    failed_while_loading_more_ = false;
    end_of_results_ = continuation_.empty();

    if (results_.empty()) {
        error_code_ = SearchErrorCode::EmptyResults;
        error_message_ = search_error_message(error_code_);
        retryable_failure_ = false;
        state_ = SearchViewState::Empty;
    } else {
        clear_problem();
        state_ = SearchViewState::Ready;
    }
    return true;
}

bool SearchModel::begin_load_more(std::uint64_t generation) {
    if (!generation_matches(generation) || continuation_.empty() || end_of_results_) return false;
    if (state_ != SearchViewState::Ready && state_ != SearchViewState::Empty) return false;

    clear_problem();
    failed_while_loading_more_ = false;
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
    end_of_results_ = continuation_.empty();
    clear_problem();
    failed_while_loading_more_ = false;
    if (!selected_identity_.empty() && !contains_identity(selected_identity_)) {
        selected_identity_.clear();
    }

    if (results_.empty()) {
        error_code_ = SearchErrorCode::EmptyResults;
        error_message_ = search_error_message(error_code_);
        state_ = SearchViewState::Empty;
    } else {
        state_ = SearchViewState::Ready;
    }
    return true;
}

bool SearchModel::fail(std::uint64_t generation, SearchErrorCode code) {
    if (!generation_matches(generation) ||
        (state_ != SearchViewState::Loading && state_ != SearchViewState::LoadingMore)) {
        return false;
    }

    failed_while_loading_more_ = state_ == SearchViewState::LoadingMore;
    if (failed_while_loading_more_) code = SearchErrorCode::ContinuationFailure;
    if (code == SearchErrorCode::None || code == SearchErrorCode::EmptyResults) {
        code = SearchErrorCode::UnsupportedResponse;
    }

    error_code_ = code;
    error_message_ = search_error_message(code);
    retryable_failure_ = retryable_code(code);
    end_of_results_ = false;
    state_ = SearchViewState::Error;
    return true;
}

bool SearchModel::begin_retry(std::uint64_t generation) {
    if (!generation_matches(generation) || state_ != SearchViewState::Error ||
        !retryable_failure_) {
        return false;
    }

    const bool retry_continuation = failed_while_loading_more_ && !continuation_.empty();
    clear_problem();
    failed_while_loading_more_ = false;
    state_ = retry_continuation ? SearchViewState::LoadingMore : SearchViewState::Loading;
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
    clear_problem();
    failed_while_loading_more_ = false;
    end_of_results_ = false;
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

void SearchModel::clear_problem() {
    error_code_ = SearchErrorCode::None;
    error_message_.clear();
    retryable_failure_ = false;
}

}  // namespace ttnx::core
