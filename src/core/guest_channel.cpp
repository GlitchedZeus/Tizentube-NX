#include "tizentube_nx/youtube/guest_channel.hpp"

#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/net/diagnostics.hpp"
#include "tizentube_nx/youtube/channel_response.hpp"

#include <utility>

namespace ttnx::youtube {
namespace {

GuestChannelResult fail(std::string error) {
    GuestChannelResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

GuestChannelResult execute_guest_channel(
    net::HttpClient& http,
    const GuestSession& session,
    const core::GuestRequest& request,
    const core::FilterPolicy& policy) {
    if (request.surface != core::BrowseSurface::Channel ||
        request.is_continuation() ||
        !core::valid_guest_request(request) ||
        !core::is_valid_channel_id(request.value)) {
        return fail("Guest Channel request is invalid.");
    }
    if (!session.usable()) return fail("Guest session is not usable.");

    const auto http_request = make_browse_request(session, request.value);
    if (!http_request) return fail("Could not build guest Channel request.");

    const auto http_result = http.perform(*http_request);
    if (!http_result.ok) {
        return fail(net::safe_public_diagnostic(
            http_result.error,
            "Guest Channel HTTP request failed."));
    }
    if (http_result.response.status_code < 200 || http_result.response.status_code >= 300) {
        return fail("Guest Channel returned a non-success HTTP status.");
    }

    auto parsed = parse_scoped_channel_response(
        http_result.response.body,
        request.value,
        policy);
    if (!parsed || !parsed.page) {
        auto result = fail(parsed.error.empty()
            ? std::string{"Guest Channel response was rejected."}
            : net::safe_public_diagnostic(
                  parsed.error,
                  "Guest Channel response was rejected."));
        result.diagnostics = std::move(parsed.diagnostics);
        return result;
    }

    GuestChannelResult result;
    result.page = std::move(parsed.page);
    result.diagnostics = std::move(parsed.diagnostics);
    return result;
}

}  // namespace ttnx::youtube
