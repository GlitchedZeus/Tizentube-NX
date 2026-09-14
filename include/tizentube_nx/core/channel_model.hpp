#pragma once

#include "tizentube_nx/core/search_result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::core {

struct ChannelIdentity {
    std::string browse_id;
    std::string title;
};

struct ChannelMetadata {
    std::string title;
    std::string handle;
    std::string subscriber_text;
    std::string description;
    std::string canonical_url;
    std::vector<ThumbnailCandidate> thumbnails;
};

struct ChannelSection {
    std::string title;
    std::vector<BrowseResult> results;
};

struct ChannelPage {
    ChannelIdentity identity;
    ChannelMetadata metadata;
    std::vector<ChannelSection> sections;
    std::vector<BrowseResult> results;
    // Architectural seam only. M2 Channel first-page UI does not expose it.
    std::string continuation;

    [[nodiscard]] bool has_more() const noexcept { return !continuation.empty(); }
};

enum class ChannelViewState {
    Idle,
    Loading,
    Ready,
    Empty,
    Error,
};

// UI-thread-owned Channel state. Workers publish only normalized ChannelPage data
// plus the generation captured by begin_load(); Borealis Views never enter here.
class ChannelModel {
public:
    [[nodiscard]] std::uint64_t begin_load(ChannelIdentity identity);
    [[nodiscard]] bool apply_page(std::uint64_t generation, ChannelPage page);
    [[nodiscard]] bool fail(std::uint64_t generation, std::string error);
    void reset();

    [[nodiscard]] bool select(std::string_view identity);
    void clear_selection();

    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] ChannelViewState state() const noexcept { return state_; }
    [[nodiscard]] const ChannelIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const ChannelPage& page() const noexcept { return page_; }
    [[nodiscard]] const ChannelMetadata& metadata() const noexcept { return page_.metadata; }
    [[nodiscard]] const std::vector<BrowseResult>& results() const noexcept { return page_.results; }
    [[nodiscard]] const std::vector<ChannelSection>& sections() const noexcept { return page_.sections; }
    [[nodiscard]] const std::string& selected_identity() const noexcept { return selected_identity_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    [[nodiscard]] bool contains_identity(std::string_view identity) const;

    std::uint64_t generation_{0};
    ChannelViewState state_{ChannelViewState::Idle};
    ChannelIdentity identity_;
    ChannelPage page_;
    std::string selected_identity_;
    std::string error_;
};

}  // namespace ttnx::core
