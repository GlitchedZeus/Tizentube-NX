#include "tizentube_nx/core/content_filter.hpp"
#include "tizentube_nx/core/url.hpp"

#include <iostream>

int main() {
    const auto clean = ttnx::core::canonicalize_youtube_video_url(
        "https://www.youtube.com/watch?v=IzJ7R4EnYmI&si=tracking&feature=shared");

    std::cout << "TizenTube NX foundation\n";
    std::cout << "Clean share URL: " << (clean ? *clean : "<invalid>") << "\n";

    const ttnx::core::ContentItem shorts{ttnx::core::ContentKind::Short, "abc", "A Short"};
    std::cout << "Shorts visible: " << (ttnx::core::should_hide(shorts) ? "no" : "yes") << "\n";
    return 0;
}
