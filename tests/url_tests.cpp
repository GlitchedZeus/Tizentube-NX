#include "tizentube_nx/core/url.hpp"

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

    constexpr const char* video_id = "IzJ7R4EnYmI";
    expect(is_valid_video_id(video_id), "ordinary video id accepted");
    expect(!is_valid_video_id("IzJ7R4EnYm"), "short video id rejected");
    expect(!is_valid_video_id("IzJ7R4EnYm%"), "encoded/unsafe video id rejected");

    const auto clean = canonical_watch_url(video_id);
    expect(clean && *clean == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "canonical video share format is exact");

    const auto dirty = canonicalize_youtube_video_url(
        "https://www.youtube.com/watch?v=IzJ7R4EnYmI&si=abcdef&feature=shared&utm_source=x&list=PLjunk&t=90");
    expect(dirty && *dirty == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "tracking, playlist and timestamp garbage is discarded");

    const auto short_link = canonicalize_youtube_video_url(
        "https://youtu.be/IzJ7R4EnYmI?si=abcdef&t=12");
    expect(short_link && *short_link == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "youtu.be input is accepted but canonical output never uses youtu.be");

    const auto old_http = canonicalize_youtube_video_url(
        "http://www.youtube.com/watch?v=IzJ7R4EnYmI");
    expect(old_http && *old_http == "https://www.youtube.com/watch?v=IzJ7R4EnYmI",
           "legacy HTTP input is normalized locally to HTTPS output");

    expect(extract_video_id("https://www.youtube.com/embed/IzJ7R4EnYmI") == video_id,
           "embed URL extracts a normal video id");
    expect(extract_video_id("https://www.youtube.com/live/IzJ7R4EnYmI?feature=share") == video_id,
           "live URL extracts a normal video id");

    expect(!extract_video_id("https://www.youtube.com/watch?feature=share"),
           "watch URL missing v is rejected");
    expect(!extract_video_id(
        "https://www.youtube.com/watch?v=IzJ7R4EnYmI&v=ABCDEFGHIJK"),
           "conflicting v values are rejected");
    expect(!extract_video_id(
        "https://www.youtube.com/watch?v=IzJ7R4EnYmI&v=IzJ7R4EnYmI"),
           "duplicate v values are rejected even when identical");
    expect(!extract_video_id(
        "https://www.youtube.com/watch?v=%49zJ7R4EnYmI"),
           "percent-encoded video id garbage is rejected");
    expect(!extract_video_id("https://www.youtube.com/watch?v=bad"),
           "malformed video id is rejected");
    expect(!extract_video_id("https://www.youtube.com/playlist?list=PLExample"),
           "unrelated YouTube playlist URL is not treated as a video");
    expect(!extract_video_id("https://example.com/watch?v=IzJ7R4EnYmI"),
           "non-YouTube URL is rejected");
    expect(!extract_video_id("https://youtube.com.evil.example/watch?v=IzJ7R4EnYmI"),
           "deceptive hostname is rejected");
    expect(!extract_video_id("https://user@www.youtube.com/watch?v=IzJ7R4EnYmI"),
           "userinfo URL is rejected");
    expect(!extract_video_id("https://www.youtube.com:443/watch?v=IzJ7R4EnYmI"),
           "non-canonical authority with explicit port is rejected");
    expect(!extract_video_id("youtube.com/watch?v=IzJ7R4EnYmI"),
           "scheme-less ambiguous input is rejected");
    expect(!extract_video_id(""), "empty URL rejected");
    expect(!extract_video_id(std::string(4097, 'x')), "absurdly large URL rejected before parsing");

    expect(is_shorts_url("https://www.youtube.com/shorts/IzJ7R4EnYmI"),
           "Shorts URL recognized");
    expect(is_shorts_url("https://m.youtube.com/reel/IzJ7R4EnYmI"),
           "reel URL recognized as Shorts-family route");
    expect(!canonicalize_youtube_video_url("https://www.youtube.com/shorts/IzJ7R4EnYmI?feature=share"),
           "Shorts URL cannot be canonicalized into a normal video");
    expect(!canonicalize_youtube_video_url("https://www.youtube.com/reel/IzJ7R4EnYmI"),
           "reel URL cannot bypass the Shorts firewall");

    constexpr const char* channel_id = "UCabcdefghijklmnopqrstuv";
    expect(is_valid_channel_id(channel_id), "stable channel id accepted");
    const auto channel = canonical_channel_url(channel_id);
    expect(channel && *channel ==
               "https://www.youtube.com/channel/UCabcdefghijklmnopqrstuv",
           "channel canonical URL uses stable /channel/ id form");
    expect(!canonical_channel_url("@handle"), "handle is not substituted for a stable channel id");

    const auto playlist = canonical_playlist_url("PL_Example-1234567890");
    expect(playlist && *playlist ==
               "https://www.youtube.com/playlist?list=PL_Example-1234567890",
           "playlist canonical URL is clean");
    expect(!canonical_playlist_url("PL bad"), "malformed playlist id rejected");

    if (failures == 0) {
        std::cout << "All canonical URL and ID validation tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
