#pragma once

#include "tizentube_nx/youtube/guest_search.hpp"

#include <string>
#include <string_view>
#include <unordered_set>

namespace ttnx::youtube {

// Host-testable owner of one logical Search query and its pagination state.
// The low-level executor remains deliberately stateless; this class is the safe
// path for callers that need continuations without allowing tokens to leak from
// an older query into a newer one.
class GuestSearchFlow {
public:
    [[nodiscard]] GuestSearchResult begin(
        net::HttpClient& http,
        const GuestSession& session,
        std::string_view query,
        const core::FilterPolicy& policy = {});

    [[nodiscard]] GuestSearchResult next(
        net::HttpClient& http,
        const GuestSession& session,
        const core::FilterPolicy& policy = {});

    void reset();

    [[nodiscard]] bool active() const noexcept { return !query_.empty(); }
    [[nodiscard]] bool can_continue() const noexcept { return !next_continuation_.empty(); }
    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    [[nodiscard]] const std::string& next_continuation() const noexcept {
        return next_continuation_;
    }

private:
    void accept_success(const core::GuestRequest& request, const core::BrowsePage& page);

    std::string query_;
    std::string next_continuation_;
    std::unordered_set<std::string> consumed_continuations_;
};

}  // namespace ttnx::youtube
