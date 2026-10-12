"""Structural isolation gates for the opt-in M4 native local-HLS mpv route."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeLocalPlayerContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.player = (ROOT / 'switch/source/m3_player_activity.cpp').read_text()
        cls.ui_header = (ROOT / 'switch/source/m3_player_activity.hpp').read_text()
        cls.make = (ROOT / 'Makefile.switch').read_text()
        cls.main = (ROOT / 'switch/source/main.cpp').read_text()

    def test_hardware_proven_frame_lifecycle_is_preserved(self):
        self.assertIn('nvgUpdateImage(vg,image_,pixels.data())', self.player)
        self.assertIn('MPV_RENDER_API_TYPE_SW', self.player)
        self.assertIn('root->hide([]{},false,0.0f)', self.player)
        self.assertIn('mpv_render_context_set_update_callback(', self.player)

    def test_no_direct_remote_playback(self):
        self.assertIn('RollingHlsSink::kPlaylist', self.player)
        self.assertIn('playback::parse_local_live_hls(data,gate)', self.player)
        self.assertIn('gate.playable_prefix()', self.player)
        self.assertIn('protocol_whitelist=file,crypto,data', self.player)
        self.assertNotIn('protocol_whitelist=http', self.player)
        self.assertNotIn('protocol_whitelist=https', self.player)
        self.assertIn('live_loaded_', self.player)
        self.assertIn('Live seek awaits verified re-prime', self.player)

    def test_switch_probe_is_compile_only_not_user_launchable(self):
        self.assertIn('TTNX_M4_PLAYER_PROBE', self.make)
        self.assertIn('TTNX_M4_ROLLING_STREAM', self.make)
        self.assertIn('TTNX_M4_PLAYER_PROBE', self.ui_header)
        self.assertNotIn('open_m4_local_hls_probe', self.main)
        self.assertIn('open_m3_completed_media()', self.main)

    def test_accepted_hls_preserves_audio_and_fd_lease(self):
        self.assertIn('playback::HlsPlaybackLease ready_lease_', self.player)
        self.assertIn('mpv_terminate_destroy(handle_)', self.player)
        self.assertIn('ready_lease_.release()', self.player)
        self.assertIn('selected_audio_track()', self.player)
        self.assertIn('SwitchHlsCacheSink::kReadyMaster', self.player)


if __name__ == '__main__':
    unittest.main()
