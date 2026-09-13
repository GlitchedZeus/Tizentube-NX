#include "tizentube_nx/core/search_error.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool contains_sensitive_marker(const std::string& value) {
    return value.find("visitor") != std::string::npos ||
           value.find("cookie") != std::string::npos ||
           value.find("token") != std::string::npos ||
           value.find("authorization") != std::string::npos;
}

}  // namespace

int main() {
    using namespace ttnx::core;

    expect(search_error_message(SearchErrorCode::NetworkUnavailable).find("network") != std::string::npos,
           "network-unavailable error has safe user-facing guidance");
    expect(search_error_message(SearchErrorCode::NetworkPolicyRejected).find("blocked") != std::string::npos,
           "policy rejection is distinguishable");
    expect(search_error_message(SearchErrorCode::HttpFailure).find("HTTP") != std::string::npos,
           "HTTP failure is distinguishable");
    expect(search_error_message(SearchErrorCode::MalformedResponse).find("safely read") != std::string::npos,
           "malformed response is distinguishable");
    expect(search_error_message(SearchErrorCode::UnsupportedResponse).find("not supported") != std::string::npos,
           "unsupported response is distinguishable");
    expect(search_error_message(SearchErrorCode::EmptyResults) == "No results found.",
           "empty result message is concise");
    expect(search_error_message(SearchErrorCode::ContinuationFailure).find("More results") != std::string::npos,
           "continuation failure is distinguishable");

    for (const auto code : {
             SearchErrorCode::NetworkUnavailable,
             SearchErrorCode::NetworkPolicyRejected,
             SearchErrorCode::HttpFailure,
             SearchErrorCode::MalformedResponse,
             SearchErrorCode::UnsupportedResponse,
             SearchErrorCode::EmptyResults,
             SearchErrorCode::ContinuationFailure}) {
        const auto message = search_error_message(code);
        expect(!message.empty(), "non-none error code has a user-facing message");
        expect(!contains_sensitive_marker(message), "UI error message contains no sensitive marker names");
        expect(message.size() < 160, "UI error message remains bounded and concise");
    }

    expect(classify_search_transport_error(
               "Outbound destination blocked by TizenTube NX network policy.") ==
               SearchErrorCode::NetworkPolicyRejected,
           "transport policy rejection maps to policy error");
    expect(classify_search_transport_error("TCP connection failed.") ==
               SearchErrorCode::NetworkUnavailable,
           "generic transport failure maps to network unavailable");
    expect(classify_search_transport_error("HTTP request returned status 503.") ==
               SearchErrorCode::HttpFailure,
           "HTTP diagnostic maps to HTTP failure");

    expect(classify_search_parse_error(
               "Search response did not contain a recognized primary or continuation payload.") ==
               SearchErrorCode::UnsupportedResponse,
           "unrecognized safe response shape maps to unsupported response");
    expect(classify_search_parse_error(
               "Search response is malformed or exceeds scope parser limits.") ==
               SearchErrorCode::MalformedResponse,
           "malformed parser diagnostic maps to malformed response");

    if (failures == 0) {
        std::cout << "All Search error taxonomy tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
