"""Switch M4 must distinguish a missing muxed VOD from an SD, audio, or
decoder failure. Numeric-only diagnostic logging must not retain CDN URLs.
"""
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
APP=(ROOT/"switch/source/main.cpp").read_text()
PLAYER=(ROOT/"switch/source/m3_player_activity.cpp").read_text()
MAKE=(ROOT/"Makefile.switch").read_text()
class HardwareFeedbackContracts(unittest.TestCase):
    def test_hls_variant_missing_is_not_decoder_failure(self):
        m4=APP.split("void start_m4_native_video(",1)[1].split("void start_m3_video(",1)[0]
        self.assertIn("HlsProbeError::VariantUnavailable",m4)
        self.assertIn("separate video/audio streaming is not implemented",m4)
        self.assertIn("describe_hls_probe_failure(",m4)
        self.assertIn("M3Stage::Hls",m4)
        self.assertIn("M3Event::Status",m4)
        self.assertNotIn("signed_url",m4)
    def test_edl_lab_separated_from_default_m3(self):
        self.assertIn("#if defined(TTNX_M3_ADAPTIVE_EDL_SEEK)",PLAYER)
        self.assertIn('use_adaptive_?"relative+exact":"relative"',PLAYER)
        self.assertIn("local_adaptive_edl(",PLAYER)
        self.assertIn("DEFINES += -DTTNX_M3_ADAPTIVE_EDL_SEEK",MAKE)
        self.assertIn('attach_external_aac(SwitchMediaCacheSink::kAdaptiveAudio)',PLAYER)
    def test_full_download_budget_gate(self):
        self.assertIn("pair_budget.admit(job.total_bytes())",APP)
        self.assertIn("pair_too_large.store(true)",APP)
        self.assertIn("job.cancel()",APP)
        self.assertIn("full download over 60MB SD limit",APP)
if __name__=="__main__":unittest.main()
