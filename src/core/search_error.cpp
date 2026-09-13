#include "tizentube_nx/core/search_error.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace ttnx::core {
namespace {

std::string lowercase(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

}  // namespace

std::string search_error_message(SearchErrorCode code) {
    switch (code) {
        case SearchErrorCode::None:
            return {};
        case SearchErrorCode::NetworkUnavailable:
            return "YouTube is unreachable right now. Check your network connection and try again.";
        case SearchErrorCode::NetworkPolicyRejected:
            return "TizenTube NX blocked this request because the destination is not approved.";
        case SearchErrorCode::HttpFailure:
            return "YouTube returned an HTTP error. Please try again.";
        case SearchErrorCode::MalformedResponse:
            return "YouTube returned data that TizenTube NX could not safely read.";
        case SearchErrorCode::UnsupportedResponse:
            return "This YouTube response format is not supported yet.";
        case SearchErrorCode::EmptyResults:
            return "No results found.";
        case SearchErrorCode::ContinuationFailure:
            return "More results could not be loaded. You can try again.";
        case SearchErrorCode::MemoryPressure:
            return "Search failed safely because there was not enough available memory. Please try again.";
        case SearchErrorCode::InternalFailure:
            return "Search failed safely because of an internal error. Please try again.";
    }
    return "Search could not be completed.";
}

SearchErrorCode classify_search_transport_error(std::string_view diagnostic) {
    const auto lower = lowercase(diagnostic);
    if (lower.find("network policy") != std::string::npos ||
        lower.find("destination blocked") != std::string::npos) {
        return SearchErrorCode::NetworkPolicyRejected;
    }
    if (lower.find("status ") != std::string::npos ||
        lower.find("http") != std::string::npos) {
        return SearchErrorCode::HttpFailure;
    }
    return SearchErrorCode::NetworkUnavailable;
}

SearchErrorCode classify_search_parse_error(std::string_view diagnostic) {
    const auto lower = lowercase(diagnostic);
    if (lower.find("did not contain a recognized") != std::string::npos ||
        lower.find("unsupported") != std::string::npos) {
        return SearchErrorCode::UnsupportedResponse;
    }
    return SearchErrorCode::MalformedResponse;
}

}  // namespace ttnx::core
