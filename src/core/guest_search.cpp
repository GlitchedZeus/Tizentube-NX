#include "tizentube_nx/youtube/guest_search.hpp"

#include "tizentube_nx/net/diagnostics.hpp"
#include "tizentube_nx/youtube/search_response.hpp"

#include <utility>

namespace ttnx::youtube {
namespace {

GuestSearchResult fail(core::SearchErrorCode code, std::string error) {
    GuestSearchResult result;
    result.error_code = code;
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
        return fail(core::SearchErrorCode::UnsupportedResponse, "Guest Search request is invalid.");
    }
    if (!session.usable()) {
        return fail(core::SearchErrorCode::UnsupportedResponse, "Guest session is not usable.");
    }

    const auto http_request = make_search_request(
        session,
        request.value,
        request.continuation);
    if (!http_request) {
        return fail(core::SearchErrorCode::UnsupportedResponse,
                    "Could not build guest Search request.");
    }

    const auto http_result = http.perform(*http_request);
    if (!http_result.ok) {
        const auto safe = net::safe_public_diagnostic(
            http_result.error,
            "Guest Search HTTP request failed.");
        return fail(core::classify_search_transport_error(safe), safe);
    }
    if (http_result.response.status_code < 200 || http_result.response.status_code >= 300) {
        return fail(core::SearchErrorCode::HttpFailure,
                    "Guest Search returned a non-success HTTP status.");
    }

    auto parsed = parse_scoped_search_response(http_result.response.body, policy);
    if (!parsed || !parsed.page) {
        // Classification may inspect the parser's internal diagnostic, but only
        // the sanitized replacement below can leave this boundary. This lets a
        // sensitive word such as "continuation" trigger redaction without
        // destroying the coarse Unsupported-vs-Malformed distinction.
        const auto code = parsed.error.empty()
            ? core::SearchErrorCode::MalformedResponse
            : core::classify_search_parse_error(parsed.error);
        const auto diagnostic = parsed.error.empty()
            ? std::string{"Guest Search response was rejected."}
            : net::safe_public_diagnostic(
                  parsed.error,
                  "Guest Search response was rejected.");
        return fail(code, diagnostic);
    }

    GuestSearchResult result;
    result.page = std::move(parsed.page);
    return result;
}

}  // namespace ttnx::youtube
