#pragma once

#include <string>
#include <string_view>

namespace ttnx::net {

// Produces a short UI/log-safe diagnostic. Messages that look like they contain
// session, cookie, authorization, continuation, or payload data are replaced by
// the caller-provided fallback rather than partially redacted and risk leakage.
[[nodiscard]] std::string safe_public_diagnostic(
    std::string_view message,
    std::string_view fallback);

}  // namespace ttnx::net
