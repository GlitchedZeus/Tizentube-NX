"""M4 experimental native ring must not regress the accepted M3 renderer.

This is a source-level integration guard in addition to the real C++ host
tests; it does not claim an on-device decode or native linker result.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLAYER = (ROOT / "switch/source/m3_player_activity.cpp").read_text()
GATE = (ROOT / "include/tizentube_nx/playback/rolling_reprime_gate.hpp").read_text()

class M4RingReprimeContract(unittest.TestCase):
    def test_isolated_to_native_opt_in_and_original_player_preserved(self):
        self.assertIn("#if defined(TTNX_M4_RING_STREAM_CB_PROBE)", PLAYER)
        self.assertIn("player_.service_ring_reprime();", PLAYER)
        self.assertIn("root->hide([]{},false,0.0f)", PLAYER)
        self.assertIn("MPV_RENDER_API_TYPE_SW", PLAYER)
        self.assertIn('SwitchMediaCacheSink::discard_session_media()', PLAYER)
        self.assertIn('SwitchHlsCacheSink::discard_session_media()', PLAYER)

    def test_end_file_requires_verified_av_and_clean_quota(self):
        event = PLAYER.split("if(e->event_id==MPV_EVENT_END_FILE)",1)[1].split(
            "if((requires_audio_gate())",1)[0]
        for item in ("MPV_END_FILE_REASON_EOF",
                     "RingReprimeGate::Stage::Watching",
                     "ring_verified_av_ && !audio_failure_latched_",
                     "snap.next_segment_index", "snap.epoch_origin_segment",
                     "stats.complete", "!stats.failed", "!stats.cancelled"):
            self.assertIn(item,event)

    def test_restart_destroys_decoder_before_reusing_sd_and_no_cdn_on_ui(self):
        service = PLAYER.split("void service_ring_reprime() {",1)[1].split(
            "    [[nodiscard]] std::uint32_t playback_seconds()",1)[0]
        self.assertLess(service.index("close();"),
                        service.index("ring_epoch_->restart_at(next)"))
        self.assertLess(service.index("ring_epoch_->restart_at(next)"),
                        service.index("ring_epoch_->start()"))
        self.assertIn("ring_reprime_.prefix_ready()",service)
        self.assertIn("if(!ring_epoch_->ring_for_decoder())return;",service)
        self.assertNotIn("stream_segment(", service)
        self.assertNotIn("https://",service)

    def test_stop_and_eof_delete_temporary_media(self):
        stop=PLAYER.split("void stop() {",1)[1].split(
            "    [[nodiscard]] bool seek_absolute",1)[0]
        self.assertIn("ring_reprime_.cancel();",stop)
        self.assertIn("close();",stop)
        self.assertIn("ring_terminal_retired_=true;",stop)
        service=PLAYER.split("void service_ring_reprime() {",1)[1].split(
            "    [[nodiscard]] std::uint32_t playback_seconds()",1)[0]
        self.assertIn("Gate::Stage::Finished",service)
        self.assertIn("ring_terminal_retired_=true;",service)

    def test_verified_current_decoder_frames_only(self):
        update=PLAYER.split("void update_ring_buffer_window() noexcept {",1)[1].split(
            "    void service_ring_reprime()",1)[0]
        self.assertIn("frames_<=epoch_frame_base_",update)
        self.assertIn("selected_video_track()",update)
        self.assertIn("selected_audio_track()",update)
        self.assertIn("advance_playback_ms",update)
        self.assertIn("last_started_",GATE)
        self.assertIn("Stage::Cancelled",GATE)

if __name__=="__main__":
    unittest.main()
