#!/usr/bin/env python3
"""Source-isolation regression gates for the experimental offline mpv NRO.

These are intentionally strict source-contract checks, not a substitute for a
compiled NRO, media codec audit, or physical hardware test.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OfflineProbeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (ROOT / "switch" / "m3_probe" / "main.cpp").read_text()
        cls.accepted = (ROOT / "switch" / "source" / "main.cpp").read_text()
        cls.make = (ROOT / "Makefile.switch").read_text()
        cls.workflow = (
            ROOT / ".github" / "workflows" / "m3-offline-probe.yml"
        ).read_text()
        cls.host_workflow = (
            ROOT / ".github" / "workflows" / "host-tests.yml"
        ).read_text()
        cls.network = (ROOT / "src" / "core" / "outbound_policy.cpp").read_text()

    def test_explicit_opt_in_and_distinct_binary(self):
        self.assertIn("SOURCES", self.make)
        self.assertIn("SOURCES     := switch/m3_probe", self.make)
        self.assertIn("TARGET      := tizentube_nx_m3_probe", self.make)
        self.assertIn("ifeq ($(TTNX_M3_OFFLINE_PROBE),1)", self.make)
        self.assertIn("TARGET\t\t:=\ttizentube_nx", self.make)
        self.assertIn("LIBS\t:= -lnx", self.make)
        self.assertIn("LIBS := $(MPV_STATIC_LIBS) -lnx", self.make)
        self.assertNotIn("mpv/client.h", self.accepted)
        self.assertNotIn("MPV_RENDER_API_TYPE_SW", self.accepted)

    def test_probe_has_no_arbitrary_media_input(self):
        self.assertIn(
            '"sdmc:/switch/TizenTube-NX/m3-test.mp4"', self.main)
        self.assertEqual(self.main.count('{"loadfile", kFixture,'), 1)
        self.assertNotIn('https://', self.main)
        self.assertNotIn('http://', self.main)
        self.assertNotIn('curl_http_client', self.main)
        self.assertNotIn('LibnxHttpClient', self.main)
        self.assertNotIn('HttpRequest', self.main)
        self.assertNotIn('swkbd', self.main)

    def test_mpv_configuration_fails_closed(self):
        for required in (
            'require_option("config", "no")',
            'require_option("load-scripts", "no")',
            'require_option("ytdl", "no")',
            'require_option("input-default-bindings", "no")',
            'require_option("demuxer-lavf-o", "protocol_whitelist=file,pipe")',
        ):
            self.assertIn(required, self.main)
        self.assertIn(
            'message_ = std::string("INIT ERROR option ")',
            self.main)
        self.assertIn("shutdown();", self.main)
        self.assertIn('mpv_error_string(rc)', self.main)
        self.assertIn('mpv_error_string(init_rc)', self.main)
        self.assertIn('mpv_error_string(render_rc)', self.main)
        self.assertIn('mpv_error_string(frame_rc)', self.main)
        self.assertIn('if (!render_ && !init()) return false;', self.main)
        self.assertNotIn('message_ = "Player not initialized"', self.main)


    def test_mpv_render_abi_and_alpha_are_explicit(self):
        self.assertIn("MPV_RENDER_API_TYPE_SW", self.main)
        self.assertIn("std::size_t stride =", self.main)
        self.assertIn("MPV_RENDER_PARAM_SW_STRIDE, &stride", self.main)
        self.assertIn('const_cast<char*>("rgb0")', self.main)
        self.assertIn("pixels_[i] = 255;", self.main)
        self.assertIn("mpv_render_context_report_swap(render_);", self.main)
        self.assertIn("new_frame_", self.main)

    def test_hardware_controls_all_visible_without_scrolling(self):
        self.assertIn('root->setPadding(12, 20, 12, 20)', self.main)
        self.assertIn('auto* first = new brls::Box(brls::Axis::ROW);', self.main)
        self.assertIn('auto* second = new brls::Box(brls::Axis::ROW);', self.main)
        self.assertIn('button->setHeight(52);', self.main)
        self.assertNotIn('new brls::ScrollingFrame()', self.main)
        self.assertIn('"A - Load / Retry"', self.main)
        self.assertIn('"Pause / Resume"', self.main)
        self.assertIn('"Seek -10s (L)"', self.main)
        self.assertIn('"Seek +10s (R)"', self.main)
        self.assertIn('"Stop (X)"', self.main)

    def test_m2_network_policy_is_still_explicit(self):
        self.assertIn(
            'return normalized == "www.youtube.com";', self.network)
        self.assertIn('host_is_or_subdomain_of(normalized, "nintendo.net")',
                      self.network)
        self.assertNotIn("googlevideo.com", self.network)

    def test_ci_uses_a_distinct_artifact(self):
        self.assertIn("TTNX_M3_OFFLINE_PROBE=1", self.workflow)
        self.assertIn("tizentube_nx_m3_probe.nro", self.workflow)
        self.assertIn("test ! -e tizentube_nx.nro", self.workflow)
        self.assertIn("tizentube-nx-m3-OFFLINE-EXPERIMENT", self.workflow)
        self.assertIn("submodules: recursive", self.workflow)
        self.assertIn("ctest", self.host_workflow)


if __name__ == "__main__":
    unittest.main()
