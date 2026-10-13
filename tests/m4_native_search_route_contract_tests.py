"""M4 explicitly opted native Search->HLS->libmpv hardware lab contract.

Structural gates complement native compilation. They are NOT proof of video
playback, HTTPS compatibility, or on-device renderer/audio continuity.
"""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/"switch/source/main.cpp").read_text()
MAKE=(ROOT/"Makefile.switch").read_text()
CI=(ROOT/".github/workflows/m3-integrated-player.yml").read_text()

class M4NativeSearchRouteContract(unittest.TestCase):
    def test_double_opt_in_and_m3_default_isolation(self):
        for x in ("TTNX_M4_NATIVE_TEST_UI","TTNX_M4_RING_STREAM_CB_PROBE",
                  "TTNX_M4_ROLLING_STREAM","TTNX_M3_INTEGRATED"):
            self.assertIn(x,MAKE)
        self.assertIn("DEFINES += -DTTNX_M4_NATIVE_TEST_UI",MAKE)
        self.assertIn('Play video (M3 experimental)',MAIN)
        self.assertIn('Play streaming (M4 hardware test)',MAIN)
        self.assertIn("start_m4_native_video(verified)",MAIN)

    def test_verified_normal_search_identity_not_id_only(self):
        handler=MAIN.split("void start_m4_native_video(",1)[1].split(
            "    void start_m3_video(",1)[0]
        for t in ("ContentKind::Video","is_valid_video_id(verified.id)",
                  "should_hide(verified)","search_model_.results()",
                  "BrowseResultKind::Video","!result.live && !result.upcoming",
                  "probe.inspect(verified.id,720,60)",
                  "create_native_ring_epoch(*plan,verified)"):
            self.assertIn(t,handler)
        self.assertIn("std::chrono::seconds(35)",handler)
        self.assertIn("epoch->ring_for_decoder()",handler)
        self.assertIn("epoch->stop_and_join()",handler)
        self.assertNotIn("open_m4_native_ring_candidate(",handler)

    def test_only_ui_completion_opens_decoder(self):
        pump=MAIN.split("void pump_m3_video() {",1)[1].split(
            "    void start_search()",1)[0]
        self.assertIn("pending_ring=std::move(pending_m4_epoch_);",pump)
        self.assertIn("media_cancel_requested_.load()",pump)
        self.assertIn("open_m4_native_ring_candidate(std::move(pending_ring))",pump)
        self.assertIn("if(success && pending_ring)",pump)
        self.assertIn("if(success && pending_media_hls_)",pump)
        self.assertLess(pump.index("if(success && pending_ring)"),
                        pump.index("if(success && pending_media_hls_)"))

    def test_shutdown_joins_native_worker_before_deleting_media(self):
        shutdown=MAIN.split("void shutdown_network() {",1)[1].split(
            "    private:",1)[0]
        self.assertLess(shutdown.index("media_worker_.join()"),
                        shutdown.index("abandoned_ring->stop_and_join()"))
        self.assertIn("pending_m4_epoch_",shutdown)
        self.assertIn("abandoned_ring->cleanup()",shutdown)

    def test_hardware_lab_only_separate_artifact_not_released(self):
        self.assertIn("TTNX_M4_NATIVE_TEST_UI=1",CI)
        self.assertIn("TARGET=tizentube_nx_m4_native_test",CI)
        self.assertIn("build-m4-native-test/m4_mpv_stream_cb_probe.o",CI)
        self.assertNotIn("tizentube_nx_m4_native_test.nro\n            M3-SHA256SUMS",CI)

if __name__=="__main__":
    unittest.main()
