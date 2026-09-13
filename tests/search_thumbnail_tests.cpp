#include "tizentube_nx/core/guest_browse.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

}  // namespace

int main() {
    using namespace ttnx::core;

    RendererRecord record;
    record.renderer = "videoRenderer";
    record.item = {ContentKind::Video, "THUMBVIDEO1", "Thumbnail test"};

    for (unsigned i = 1; i <= 12; ++i) {
        record.thumbnails.push_back({
            "https://i.ytimg.com/candidate-" + std::to_string(i) + ".jpg",
            i * 100,
            i * 50,
        });
    }
    // Duplicates and malformed candidates must not consume normalized slots.
    record.thumbnails.push_back({"https://i.ytimg.com/candidate-12.jpg", 9999, 9999});
    record.thumbnails.push_back({"http://i.ytimg.com/not-https.jpg", 9999, 9999});
    record.thumbnails.push_back({"https://i.ytimg.com/contains space.jpg", 9999, 9999});
    record.thumbnails.push_back({"https://i.ytimg.com/" + std::string(2050, 'x'), 9999, 9999});
    record.thumbnail_url = "https://i.ytimg.com/legacy-fallback.jpg";

    const auto page = sanitize_guest_page({record}, "");
    expect(page.results.size() == 1, "thumbnail metadata cannot invalidate a valid result");
    if (!page.results.empty()) {
        const auto& result = page.results.front();
        expect(result.thumbnails.size() == 8,
               "normalized thumbnail candidates are capped at eight");
        expect(result.thumbnails.front().url == "https://i.ytimg.com/candidate-12.jpg",
               "largest valid candidate is preferred even when it appears after the first eight inputs");
        expect(result.thumbnails.front().width == 1200 && result.thumbnails.front().height == 600,
               "preferred candidate retains source dimensions");
        expect(result.thumbnail_url == result.thumbnails.front().url,
               "compatibility thumbnail mirrors deterministic preferred candidate");

        bool found_duplicate = false;
        bool found_invalid = false;
        for (std::size_t i = 0; i < result.thumbnails.size(); ++i) {
            for (std::size_t j = i + 1; j < result.thumbnails.size(); ++j) {
                if (result.thumbnails[i].url == result.thumbnails[j].url) found_duplicate = true;
            }
            if (!result.thumbnails[i].url.starts_with("https://") ||
                result.thumbnails[i].url.find(' ') != std::string::npos ||
                result.thumbnails[i].url.size() > 2048) {
                found_invalid = true;
            }
        }
        expect(!found_duplicate, "normalized thumbnail URLs are deduplicated");
        expect(!found_invalid, "malformed thumbnail references are discarded");
    }

    RendererRecord unknown_dimensions;
    unknown_dimensions.renderer = "videoRenderer";
    unknown_dimensions.item = {ContentKind::Video, "THUMBVIDEO2", "Unknown dimensions"};
    unknown_dimensions.thumbnails = {
        {"https://i.ytimg.com/z.jpg", 0, 0},
        {"https://i.ytimg.com/a.jpg", 0, 0},
    };
    const auto deterministic = sanitize_guest_page({unknown_dimensions}, "");
    expect(deterministic.results.size() == 1, "unknown thumbnail dimensions remain usable");
    if (!deterministic.results.empty()) {
        expect(deterministic.results.front().thumbnails.front().url == "https://i.ytimg.com/a.jpg",
               "equal/unknown dimensions use stable URL ordering");
    }

    RendererRecord blocked;
    blocked.renderer = "reelItemRenderer";
    blocked.item = {ContentKind::Video, "BLOCKTHUMB1", "Blocked"};
    blocked.thumbnails = {{"https://i.ytimg.com/very-valid.jpg", 1920, 1080}};
    const auto blocked_page = sanitize_guest_page({blocked}, "");
    expect(blocked_page.results.empty(),
           "valid thumbnail metadata cannot let a disguised Short bypass the firewall");

    if (failures == 0) {
        std::cout << "All normalized thumbnail tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
