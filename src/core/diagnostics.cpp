#include "tizentube_nx/net/diagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace ttnx::net {
namespace {

constexpr std::size_t kMaxPublicDiagnosticBytes = 240;

std::string lowercase(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool contains_sensitive_marker(std::string_view lower) {
    for (const auto marker : {
             std::string_view{"authorization"},
             std::string_view{"bearer"},
             std::string_view{"cookie"},
             std::string_view{"set-cookie"},
             std::string_view{"visitor"},
             std::string_view{"x-goog-visitor"},
             std::string_view{"continuation"},
             std::string_view{"refresh_token"},
             std::string_view{"access_token"},
             std::string_view{"device_code"},
             std::string_view{"request body"},
             std::string_view{"response body"},
             std::string_view{"payload"},
         }) {
        if (lower.find(marker) != std::string_view::npos) return true;
    }
    return false;
}

std::string clean(std::string_view value) {
    std::string out;
    out.reserve(std::min(value.size(), kMaxPublicDiagnosticBytes));
    bool previous_space = false;
    for (const unsigned char c : value) {
        if (out.size() >= kMaxPublicDiagnosticBytes) break;
        if (c < 0x20 || c == 0x7f) {
            if (!previous_space && !out.empty()) out.push_back(' ');
            previous_space = true;
            continue;
        }
        out.push_back(static_cast<char>(c));
        previous_space = c == ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}  // namespace

std::string safe_public_diagnostic(
    std::string_view message,
    std::string_view fallback) {
    if (message.empty()) return clean(fallback);
    if (contains_sensitive_marker(lowercase(message))) return clean(fallback);

    auto output = clean(message);
    if (output.empty()) output = clean(fallback);
    return output;
}

}  // namespace ttnx::net
