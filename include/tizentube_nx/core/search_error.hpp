#pragma once

#include <string>
#include <string_view>

namespace ttnx::core {

// UI-facing classification only. It contains no request/response payloads,
// tokens, cookies, visitor identifiers or other technical diagnostics.
enum class SearchErrorCode {
    None,
    NetworkUnavailable,
    NetworkPolicyRejected,
    HttpFailure,
    MalformedResponse,
    UnsupportedResponse,
    EmptyResults,
    ContinuationFailure,
};

[[nodiscard]] std::string search_error_message(SearchErrorCode code);

// Maps already-sanitized transport/parser diagnostics into coarse UI classes.
// The returned classification is safe to expose; the input diagnostic is not.
[[nodiscard]] SearchErrorCode classify_search_transport_error(std::string_view diagnostic);
[[nodiscard]] SearchErrorCode classify_search_parse_error(std::string_view diagnostic);

}  // namespace ttnx::core
