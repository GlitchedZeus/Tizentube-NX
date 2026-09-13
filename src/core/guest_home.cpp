#include "tizentube_nx/youtube/guest_home.hpp"

#include "tizentube_nx/net/diagnostics.hpp"
#include "tizentube_nx/youtube/home_response.hpp"

#include <utility>

namespace ttnx::youtube {
namespace {

GuestHomeResult fail(std::string error) {
    GuestHomeResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

GuestHomeResult execute_guest_home(
    net::HttpClient& http,
    const GuestSession& session,
    const core::GuestRequest& request,
    const core::FilterPolicy& policy) {
    if (request.surface != core::BrowseSurface::Home || !core::valid_guest_request(request)) {
        return fail("Guest Home request is invalid.");
    }
    if (!session.usable()) {
        return fail("Guest session is not usable.");
    }

    const auto http_request = make_home_request(session, request.continuation);
    if (!http_request) {
        return fail("Could not build guest Home request.");
    }

    const auto http_result = http.perform(*http_request);
    if (!http_result.ok) {
        return fail(net::safe_public_diagnostic(
            http_result.error,
            "Guest Home HTTP request failed."));
    }
    if (http_result.response.status_code < 200 || http_result.response.status_code >= 300) {
        return fail("Guest Home returned a non-success HTTP status.");
    }

    auto parsed = parse_scoped_home_response(http_result.response.body, policy);
    if (!parsed || !parsed.page) {
        auto result = fail(parsed.error.empty()
            ? std::string{"Guest Home response was rejected."}
            : net::safe_public_diagnostic(
                  parsed.error,
                  "Guest Home response was rejected."));
        result.diagnostics = std::move(parsed.diagnostics);
        return result;
    }

    GuestHomeResult result;
    result.page = std::move(parsed.page);
    result.diagnostics = std::move(parsed.diagnostics);
    return result;
}

}  // namespace ttnx::youtube
