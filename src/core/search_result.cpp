#include "tizentube_nx/core/search_result.hpp"

namespace ttnx::core {

std::string browse_result_identity(const BrowseResult& result) {
    if (result.id.empty()) return {};

    switch (result.kind) {
        case BrowseResultKind::Video:
            return "video:" + result.id;
        case BrowseResultKind::Channel:
            return "channel:" + result.id;
        case BrowseResultKind::Playlist:
            return "playlist:" + result.id;
    }
    return {};
}

}  // namespace ttnx::core
