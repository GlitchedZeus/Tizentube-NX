#include "tizentube_nx/core/channel_model.hpp"

#include "tizentube_nx/core/search_result.hpp"

#include <algorithm>
#include <utility>

namespace ttnx::core {
namespace {

constexpr std::size_t kMaxChannelErrorBytes = 512;

std::string bounded_error(std::string error) {
    if (error.size() > kMaxChannelErrorBytes) error.resize(kMaxChannelErrorBytes);
    return error;
}

}  // namespace

std::uint64_t ChannelModel::begin_load(ChannelIdentity identity) {
    ++generation_;
    state_ = ChannelViewState::Loading;
    identity_ = std::move(identity);
    page_ = {};
    page_.identity = identity_;
    selected_identity_.clear();
    error_.clear();
    return generation_;
}

bool ChannelModel::apply_page(std::uint64_t generation, ChannelPage page) {
    if (generation != generation_ || state_ != ChannelViewState::Loading) return false;

    if (page.identity.browse_id.empty()) page.identity.browse_id = identity_.browse_id;
    if (page.identity.title.empty()) page.identity.title = identity_.title;
    if (page.metadata.title.empty()) page.metadata.title = page.identity.title;
    identity_ = page.identity;
    page_ = std::move(page);
    state_ = page_.results.empty() ? ChannelViewState::Empty : ChannelViewState::Ready;
    error_.clear();

    if (!selected_identity_.empty() && !contains_identity(selected_identity_)) {
        selected_identity_.clear();
    }
    return true;
}

bool ChannelModel::fail(std::uint64_t generation, std::string error) {
    if (generation != generation_ || state_ != ChannelViewState::Loading) return false;
    state_ = ChannelViewState::Error;
    error_ = bounded_error(std::move(error));
    return true;
}

void ChannelModel::reset() {
    ++generation_;
    state_ = ChannelViewState::Idle;
    identity_ = {};
    page_ = {};
    selected_identity_.clear();
    error_.clear();
}

bool ChannelModel::select(std::string_view identity) {
    if (identity.empty() || !contains_identity(identity)) return false;
    selected_identity_ = std::string(identity);
    return true;
}

void ChannelModel::clear_selection() {
    selected_identity_.clear();
}

bool ChannelModel::contains_identity(std::string_view identity) const {
    return std::any_of(page_.results.begin(), page_.results.end(), [&](const BrowseResult& result) {
        return browse_result_identity(result) == identity;
    });
}

}  // namespace ttnx::core
