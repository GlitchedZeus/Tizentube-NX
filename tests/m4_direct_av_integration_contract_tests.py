"""Regression contracts for M4's separate AAC/H264 RAM-only VOD path.

These tests intentionally audit the REAL native route, not a synthetic mock.
Byte-level media behavior is covered by the C++ tests and host sanitizers.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "switch/source/main.cpp").read_text()
PLAYER = (ROOT / "switch/source/m3_player_activity.cpp").read_text()
MAKE = (ROOT / "Makefile.switch").read_text()
NATIVE = (ROOT / "switch/source/m4_direct_av.hpp").read_text()
TRACK = (ROOT / "include/tizentube_nx/playback/bounded_direct_track.hpp").read_text()
BRIDGE = (ROOT / "include/tizentube_nx/playback/direct_av_mpv_bridge.hpp").read_text()
POLICY = (ROOT / "include/tizentube_nx/playback/direct_av_plan.hpp").read_text()


class DirectVODIntegration(unittest.TestCase):
    def test_hls_missing_is_only_direct_fallback_reason(self):
        route = MAIN.split("void start_m4_native_video(", 1)[1].split(
            "void start_m3_video(", 1)[0]
        self.assertIn("HlsProbeError::VariantUnavailable", route)
        self.assertIn("GuestDirectExtractor direct_source(", route)
        self.assertIn("admit_direct_av_plan(*extracted,verified)", route)
        self.assertIn("NativeDirectAvSession::create(", route)
        self.assertIn("media_step_.store(7)", route)
        self.assertIn("pending_m4_direct_=std::move(direct)", route)

    def test_standard_m3_isolation(self):
        self.assertIn("ifeq ($(TTNX_M4_NATIVE_TEST_UI),1)", MAKE)
        self.assertIn("DEFINES += -DTTNX_M4_DIRECT_AV_PROBE", MAKE)
        self.assertIn("#if defined(TTNX_M4_DIRECT_AV_PROBE)", PLAYER)
        self.assertIn("protocol_whitelist=file,ttnxav", PLAYER)
        self.assertIn("open_m4_native_direct_candidate(", PLAYER)
        self.assertIn("open_m3_completed_adaptive(", PLAYER)

    def test_no_mpv_network_and_no_sd_video(self):
        self.assertIn('kScheme="ttnxav"', BRIDGE)
        self.assertIn('kVideo="ttnxav://video"', BRIDGE)
        self.assertIn('kAudio="ttnxav://audio"', BRIDGE)
        self.assertIn("std::to_string(video.size())", BRIDGE)
        self.assertIn("!new_stream", BRIDGE)
        self.assertNotIn("https://", BRIDGE)
        self.assertNotIn("std::fopen(", TRACK)
        self.assertNotIn("::open(", TRACK)
        self.assertNotIn("sdmc:", TRACK)
        self.assertIn("kWindow=256ULL*1024", TRACK)
        self.assertIn("MediaChunkReader& reader_", TRACK)
        self.assertIn("video_reader_(video_http_)", NATIVE)
        self.assertIn("audio_reader_(audio_http_)", NATIVE)
        self.assertIn("direct_session_->cancel()", PLAYER)
        self.assertIn("no SD media", PLAYER)

    def test_source_identity_and_no_shorts(self):
        self.assertIn("normal_search.kind!=core::ContentKind::Video", POLICY)
        self.assertIn("core::should_hide(normal_search)", POLICY)
        self.assertIn("extraction.video_id!=normal_search.id", POLICY)
        self.assertIn("extraction.is_short || extraction.is_live", POLICY)
        self.assertIn("VideoCodec::H264", POLICY)
        self.assertIn("AudioCodec::Aac", POLICY)
        self.assertIn("is_reviewable_googlevideo_https_url(s.video_url)", POLICY)
        self.assertIn("is_reviewable_googlevideo_https_url(s.audio_url)", POLICY)


if __name__ == "__main__":
    unittest.main()
