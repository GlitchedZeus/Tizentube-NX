#include "tizentube_nx/youtube/guest_search.hpp"

#include "tizentube_nx/youtube/search_response.hpp"

#include <utility>

namespace ttnx::youtube {
namespace {

GuestSearchResult fail(std::string error) {
    GuestSearchResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

GuestSearchResult execute_guest_search(
    net::HttpClient& http,
    const GuestSession& session,
    const core::GuestRequest& request,
    const core::FilterPolicy& policy) {
    if (request.surface != core::BrowseSurface::Search || !core::valid_guest_request(request)) {
        return fail("Guest Search request is invalid.");
    }
    if (!session.usable()) return fail("Guest session is not usable.");

    const auto http_request = make_search_request(
        session,
        request.value,
        request.continuation);
    if (!http_request) return fail("Could not build guest Search request.");

    const auto http_result = http.perform(*http_request);
    if (!http_result.ok) {
        return fail(http_result.error.empty()
            ? "Guest Search HTTP request failed."
            : http_result.error);
    }
    if (http_result.response.status_code < 200 || http_result.response.status_code >= 300) {
        return fail("Guest Search returned a non-success HTTP status.");
    }

    auto parsed = parse_scoped_search_response(http_result.response.body, policy);
    if (!parsed || !parsed.page) {
        return fail(parsed.error.empty()
            ? "Guest Search response was rejected."
            : parsed.error);
    }

    GuestSearchResult result;
    result.page = std::move(parsed.page);
    return result;
}

}  // namespace ttnx::youtube
