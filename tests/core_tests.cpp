#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/ui/navigation.hpp"

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

}  // namespace

int main() {
    using namespace ttnx::core;

    expect(is_valid_video_id("IzJ7R4EnYmI"), "valid YouTube ID accepted");
    expect(!is_valid_video_id("too-short"), "invalid YouTube ID rejected");

    const auto canonical = canonical_watch_url("IzJ7R4EnYmI");
    expect(canonical && *canonical == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "canonical watch URL format");

    const auto dirty = canonicalize_youtube_video_url(
        "https://www.youtube.com/watch?v=IzJ7R4EnYmI&si=abcdef&feature=shared&utm_source=x");
    expect(dirty && *dirty == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "tracking parameters are discarded");

    const auto short_link = canonicalize_youtube_video_url("https://youtu.be/IzJ7R4EnYmI?si=abcdef");
    expect(short_link && *short_link == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "youtu.be is normalized");

    expect(is_shorts_url("https://www.youtube.com/shorts/IzJ7R4EnYmI"),
           "shorts URL recognized");
    expect(!canonicalize_youtube_video_url("https://www.youtube.com/shorts/IzJ7R4EnYmI"),
           "shorts URL rejected rather than surfaced");

    FilterPolicy relaxed;
    relaxed.hide_shorts = false; // must not override the hard invariant
    relaxed.hide_promoted = false;
    relaxed.hide_shopping = false;

    expect(should_hide({ContentKind::Short, "id", "short"}, relaxed),
           "Shorts stay hidden even if a future setting is misconfigured");
    expect(should_hide({ContentKind::Video, "id", "ad", true, false}),
           "promoted content hidden by default");
    expect(!should_hide({ContentKind::Video, "id", "normal"}),
           "normal video remains visible");

    using namespace ttnx::ui;
    expect(kRootNavigation.size() == 5, "root navigation contains exactly five sections");
    expect(root_navigation_contains("Home"), "Home root section exists");
    expect(root_navigation_contains("Search"), "Search root section exists");
    expect(root_navigation_contains("Subscriptions"), "Subscriptions root section exists");
    expect(root_navigation_contains("Library"), "Library root section exists");
    expect(root_navigation_contains("Settings"), "Settings root section exists");
    expect(!root_navigation_contains("Shorts"), "Shorts can never become a root navigation section");

    if (failures == 0) {
        std::cout << "All TizenTube NX core tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
