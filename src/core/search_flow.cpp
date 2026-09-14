#include "tizentube_nx/youtube/search_flow.hpp"

#include <utility>

namespace ttnx::youtube {
namespace {

GuestSearchResult flow_fail(std::string error) {
    GuestSearchResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

GuestSearchResult GuestSearchFlow::begin(
    net::HttpClient& http,
    const GuestSession& session,
    std::string_view query,
    const core::FilterPolicy& policy) {
    reset();
    if (query.empty()) return flow_fail("Guest Search query is empty.");

    query_ = std::string(query);
    core::GuestRequest request;
    request.surface = core::BrowseSurface::Search;
    request.value = query_;

    auto result = execute_guest_search(http, session, request, policy);
    if (!result.page) {
        // A failed initial request must not leave a half-active query whose
        // continuation state could be mistaken for a valid search session.
        reset();
        return result;
    }

    accept_success(request, *result.page);
    return result;
}

GuestSearchResult GuestSearchFlow::next(
    net::HttpClient& http,
    const GuestSession& session,
    const core::FilterPolicy& policy) {
    if (!active()) return flow_fail("Guest Search flow has no active query.");
    if (next_continuation_.empty()) {
        return flow_fail("Guest Search flow has no continuation token.");
    }

    // Build the request exclusively from state captured from the current
    // successful query. Callers cannot inject a token from another query.
    core::GuestRequest request;
    request.surface = core::BrowseSurface::Search;
    request.value = query_;
    request.continuation = next_continuation_;

    auto result = execute_guest_search(http, session, request, policy);
    if (!result.page) {
        // Keep the current token available after transport/parse failure so a
        // caller can deliberately retry; failure never advances pagination.
        return result;
    }

    accept_success(request, *result.page);
    return result;
}

void GuestSearchFlow::reset() {
    query_.clear();
    next_continuation_.clear();
    consumed_continuations_.clear();
}

void GuestSearchFlow::accept_success(
    const core::GuestRequest& request,
    const core::BrowsePage& page) {
    if (!request.continuation.empty()) {
        consumed_continuations_.insert(request.continuation);
    }

    const auto& candidate = page.continuation;
    // Stop pagination loops if YouTube repeats the token that was just used or
    // any older token already consumed in this query. An empty token naturally
    // means the response has no further page.
    if (candidate.empty() || candidate == request.continuation ||
        consumed_continuations_.contains(candidate)) {
        next_continuation_.clear();
        return;
    }

    next_continuation_ = candidate;
}

}  // namespace ttnx::youtube
