#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/playback/sponsorblock.hpp"
#include "tizentube_nx/net/outbound_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ttnx::playback;

namespace {

int failures = 0;
void check(bool pass, const char* description) {
    if (!pass) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}
constexpr const char* kVideo = "https://r1---sn-test.googlevideo.com/videoplayback?foo=123";
constexpr const char* kAudio = "https://r2---sn-test.googlevideo.com/videoplayback?foo=456";

VideoFormat video(
    std::string url = kVideo,
    VideoCodec codec = VideoCodec::H264,
    unsigned height = 720,
    AudioCodec muxed = AudioCodec::Unknown) {
    return {std::move(url), codec, muxed, 1280, height, 30, 8, 2'000'000, false, false};
}
AudioFormat audio(
    std::string url = kAudio,
    AudioCodec codec = AudioCodec::Aac) {
    return {std::move(url), codec, 128'000, false};
}
ExtractionResult result() {
    ExtractionResult r;
    r.video_id = "IzJ7R4EnYmI";
    r.duration_ms = 480'000;
    r.video.push_back(video());
    r.audio.push_back(audio());
    return r;
}

class FakePlayer final : public PlayerBackend {
public:
    bool load_ok{true};
    bool pause_ok{true};
    bool seek_ok{true};
    int loads{0};
    int stops{0};
    int pauses{0};
    int seeks{0};
    bool load(const SelectedStreams&) override { ++loads; return load_ok; }
    bool pause(bool) override { ++pauses; return pause_ok; }
    bool seek(std::uint64_t) override { ++seeks; return seek_ok; }
    void stop() override { ++stops; }
};

void url_policy_tests() {
    using ttnx::net::is_allowed_outbound_host;
    check(is_reviewable_googlevideo_https_url(kVideo), "normal signed media URL accepted structurally");
    check(is_reviewable_googlevideo_https_url(
        "https://R1.GoogleVideo.Com:443/videoplayback?range=0-10"),
        "case-insensitive host and explicit 443 accepted");
    const std::vector<std::string> bad = {
        "", "http://r1.googlevideo.com/videoplayback",
        "https://googlevideo.com/videoplayback",
        "https://evilgooglevideo.com/videoplayback",
        "https://googlevideo.com.evil.test/videoplayback",
        "https://r1.googlevideo.com.evil.test/videoplayback",
        "https://r1.googlevideo.com:444/videoplayback",
        "https://user@r1.googlevideo.com/videoplayback",
        "https://r1.googlevideo.com@accounts.nintendo.com/videoplayback",
        "https://127.0.0.1/videoplayback",
        "https://[::1]/videoplayback",
        "https://r1.googlevideo.com./videoplayback",
        "https://r1..googlevideo.com/videoplayback",
        "https://-r1.googlevideo.com/videoplayback",
        "https://r1.googlevideo.com/other",
        "https://r1.googlevideo.com/videoplayback#fragment",
        "https://r1.googlevideo.com/videoplayback\nX-Injected: yes",
        "https://r1.googlevideo.com:443:444/videoplayback",
    };
    for (const auto& url : bad) {
        check(!is_reviewable_googlevideo_https_url(url), "unsafe media URLs rejected");
    }
    check(!is_allowed_outbound_host("r1.googlevideo.com"),
          "M2 network allowlist STILL denies media destinations");
    check(!is_allowed_outbound_host("accounts.nintendo.com"), "Nintendo is hard denied");
}

void selector_tests() {
    auto r = result();
    auto s = select_streams(r);
    check(s.streams && s.error == SelectionError::None, "normal adaptive selection");
    check(s.streams && s.streams->adaptive &&
          s.streams->audio_url == kAudio, "separate audio source preserved");
    check(s.streams && s.streams->video_codec == VideoCodec::H264 &&
          s.streams->audio_codec == AudioCodec::Aac, "H264/AAC preferred");

    r.video.push_back(video(kVideo, VideoCodec::Av1, 1080));
    check(select_streams(r).streams &&
          select_streams(r).streams->video_codec == VideoCodec::H264,
          "AV1 not selected on Switch");
    r.video.push_back(video(kVideo, VideoCodec::Vp9, 1080));
    check(select_streams(r).streams &&
          select_streams(r).streams->video_codec == VideoCodec::H264,
          "VP9 hardware unverified by default");
    QualityPolicy vp9;
    vp9.permit_vp9 = true;
    check(select_streams(r, vp9).streams &&
          select_streams(r, vp9).streams->video_codec == VideoCodec::H264,
          "H264 remains preferred even when VP9 enabled");

    auto short_result = result();
    short_result.is_short = true;
    check(select_streams(short_result).error == SelectionError::ShortsForbidden,
          "Shorts blocked before selection");

    auto bad_id = result();
    bad_id.video_id = "not a valid ID";
    check(select_streams(bad_id).error == SelectionError::InvalidVideoId,
          "invalid IDs cannot reach player");

    auto unsafe = result();
    unsafe.video.front().url = "https://accounts.nintendo.com/videoplayback";
    check(select_streams(unsafe).error == SelectionError::UnsafeMediaUrl,
          "unsafe video host rejected");

    auto muxed = result();
    muxed.audio.clear();
    muxed.video.front().muxed_audio = AudioCodec::Aac;
    auto selected_muxed = select_streams(muxed);
    check(selected_muxed.streams && !selected_muxed.streams->adaptive &&
          selected_muxed.streams->audio_url.empty(), "muxed fallback works");

    muxed.video.front().hdr = true;
    check(!select_streams(muxed).streams, "HDR is not silently selected");
    muxed.video.front().hdr = false;
    muxed.video.front().bit_depth = 10;
    check(!select_streams(muxed).streams, "10-bit video unverified");
    muxed.video.front().bit_depth = 8;
    muxed.video.front().drm = true;
    check(!select_streams(muxed).streams, "DRM video never falls back");
    auto r2 = result();
    r2.duration_ms = 8ULL * 24 * 60 * 60 * 1000;
    check(select_streams(r2).error == SelectionError::UnusableDuration,
          "unbounded duration rejected");
    r2 = result();
    r2.audio.front().url = "https://www.youtube.com/videoplayback";
    check(!select_streams(r2).streams, "invalid separate audio cannot pass");
    r2 = result();
    r2.video.resize(513);
    check(!select_streams(r2).streams, "overlarge format arrays rejected");
}

void player_tests() {
    FakePlayer fake;
    PlaybackSession session(fake);
    check(session.state() == PlayerState::Idle, "initially idle");
    check(!session.pause() && !session.seek(1000), "inactive controls are inert");
    auto stream = select_streams(result()).streams;
    check(stream.has_value(), "fixture selects");
    if (!stream) return;
    check(session.play(*stream), "play loads backend");
    check(session.state() == PlayerState::Playing && fake.loads == 1, "playing state");
    check(session.pause() && session.state() == PlayerState::Paused, "pause state");
    check(!session.pause(), "double pause rejected");
    check(session.seek(3000) && fake.seeks == 1, "seek while paused");
    check(!session.seek(stream->duration_ms + 1), "seek past end rejected");
    check(session.resume() && session.state() == PlayerState::Playing, "resume state");
    session.stop();
    check(session.state() == PlayerState::Idle, "stop resets session");
    fake.load_ok = false;
    check(!session.play(*stream) && session.state() == PlayerState::Error,
          "failed backend explicitly errors");
    fake.load_ok = true;
    stream->video_url = "https://malicious.example/videoplayback";
    check(!session.play(*stream) && fake.loads == 2, "unsafe media never reaches backend");
    stream->video_url = kVideo;
    stream->is_live = true;
    check(session.play(*stream) && !session.seek(1000), "live seeking blocked until implemented");
}

void sponsorblock_tests() {
    SponsorBlockEngine engine;
    check(engine.mode(SegmentCategory::Sponsor) == SkipMode::Disabled,
          "disabled by default");
    engine.set_mode(SegmentCategory::Sponsor, SkipMode::Auto);
    engine.set_mode(SegmentCategory::Intro, SkipMode::Ask);
    engine.set_segments({
        {SegmentCategory::Sponsor, 1000, 3000},
        {SegmentCategory::Sponsor, 2500, 4000},
        {SegmentCategory::Intro, 2000, 3500},
        {SegmentCategory::Sponsor, 5000, 5000},
        {SegmentCategory::Sponsor, 9000, 12000}
    }, 10000);
    check(engine.segments().size() == 2, "same-category overlap merged; invalid dropped");
    check(engine.action_at(500).type == SkipActionType::None, "no premature skip");
    auto action = engine.action_at(2100);
    check(action.type == SkipActionType::Skip && action.end_ms == 4000,
          "Auto wins over Ask in overlapping segments");
    engine.mark_handled(action);
    check(engine.action_at(2200).type == SkipActionType::Offer,
          "handled auto suppressed, remaining Ask available");
    check(engine.action_at(4001).type == SkipActionType::None,
          "outside segment clears handled state");
    check(engine.action_at(1100).type == SkipActionType::Skip,
          "rewind rearms after leaving range");
    engine.set_mode(SegmentCategory::Sponsor, SkipMode::Disabled);
    check(engine.action_at(1100).type == SkipActionType::None,
          "category disable works");
    engine.set_segments(
        std::vector<Segment>(2049, {SegmentCategory::Intro, 0, 1}), 100);
    check(engine.segments().empty(), "oversized remote segments rejected");
}
} // namespace

int main() {
    url_policy_tests();
    selector_tests();
    player_tests();
    sponsorblock_tests();
    if (failures) {
        std::cerr << failures << " M3 tests failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "M3 playback, network guard and SponsorBlock tests passed.\n";
    return EXIT_SUCCESS;
}
