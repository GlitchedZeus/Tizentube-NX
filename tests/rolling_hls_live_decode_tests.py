"""Real H.264/AAC progressive local-HLS decode, bounded SD, zero-cache exit.

Offline only: source media is created with FFmpeg's testsrc2 and sine
filters. No YouTube, requests, signed URLs, user data or live network.
"""
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCAL_PLAYLIST = pathlib.Path(
    "/tmp/ttnx-m4-rolling-test/switch/TizenTube-NX/media-cache/rolling/stream.m3u8"
)


def command(argv, timeout=40):
    return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)


def until(predicate, seconds=12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return predicate()


class RollingHlsLiveDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("ffmpeg") or not shutil.which("g++"):
            raise unittest.SkipTest("FFmpeg and a C++20 host compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="ttnx-live-av-")
        cls.directory = pathlib.Path(cls.temp.name)
        cls.media = cls.directory / "media"
        cls.media.mkdir()
        cls.binary = cls.directory / "rolling_hls_live_writer"
        sanitizer = os.environ.get("TTNX_M4_LIVE_SANITIZE") == "1"
        flags = (["-O1", "-g", "-fsanitize=address,undefined",
                  "-fno-omit-frame-pointer"] if sanitizer else ["-O2"])
        build = command([
            "g++", "-std=c++20", *flags, "-Wall", "-Wextra", "-Werror",
            "-DTTNX_M4_ROLLING_HOST_TEST=1", "-Iinclude", "-Iswitch/source",
            "tests/rolling_hls_live_writer.cpp",
            "switch/source/rolling_hls_sink.cpp", "-o", str(cls.binary)
        ])
        if build.returncode:
            raise AssertionError("native host writer compilation failed: " + build.stderr)
        encode = command([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30",
            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100",
            "-t", "12", "-c:v", "libx264", "-preset", "ultrafast",
            "-g", "60", "-keyint_min", "60", "-sc_threshold", "0",
            "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "96k",
            "-f", "hls", "-hls_time", "2", "-hls_segment_type", "mpegts",
            "-hls_playlist_type", "vod", "-hls_segment_filename",
            str(cls.media / "media_%05d.ts"), str(cls.media / "input.m3u8")
        ])
        if encode.returncode:
            raise AssertionError("offline H.264/AAC fixture failed: " + encode.stderr)
        parts = sorted(cls.media.glob("media_*.ts"))
        if len(parts) != 6 or any(not p.stat().st_size for p in parts):
            raise AssertionError("Expected six independent two-second TS segments")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        shutil.rmtree("/tmp/ttnx-m4-rolling-test", ignore_errors=True)
        self.control = self.directory / "signal"
        self.control.mkdir(exist_ok=True)
        for f in self.control.iterdir():
            f.unlink()
        self.writer = subprocess.Popen(
            [str(self.binary), str(self.media), str(self.control)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            cwd=ROOT
        )
        self.assertTrue(until(lambda: LOCAL_PLAYLIST.exists()),
                        "First segment never became available")

    def tearDown(self):
        (self.control / "decoder_closed").touch()
        try:
            out, err = self.writer.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            self.writer.kill()
            out, err = self.writer.communicate()
            self.fail("Rolling producer hung during shutdown: " + err)
        self.assertEqual(self.writer.returncode, 0, out + err)
        self.assertFalse(LOCAL_PLAYLIST.parent.exists(),
                         "Watched video must leave ZERO temporary media files")

    def test_audio_video_decodes_during_growing_playlist(self):
        # Ensure genuine early playback: the playlist is not yet complete.
        playlist = LOCAL_PLAYLIST.read_text()
        self.assertNotIn("#EXT-X-ENDLIST", playlist)
        self.assertNotIn("googlevideo", playlist)
        self.assertNotIn("http", playlist.lower())
        self.assertTrue(until(lambda: "v_00001.ts" in LOCAL_PLAYLIST.read_text()))
        decoder = command([
            "ffmpeg", "-hide_banner", "-nostats", "-loglevel", "error",
            "-protocol_whitelist", "file,crypto,data", "-i", str(LOCAL_PLAYLIST),
            "-map", "0:v:0", "-map", "0:a:0",
            "-progress", "pipe:1", "-f", "null", "-"
        ], timeout=40)
        self.assertEqual(decoder.returncode, 0, decoder.stderr)
        us = [int(n) for n in re.findall(r"out_time_us=(\d+)", decoder.stdout)]
        self.assertTrue(us and max(us) >= 11_000_000,
                        "Decoder did not actually consume the growing 12-second video")
        self.assertLessEqual(sum(p.stat().st_size for p in LOCAL_PLAYLIST.parent.iterdir()),
                             24 * 1024 * 1024,
                             "Live app-owned buffer exceeded reserved SD quota")

    def test_playback_survives_old_segment_eviction(self):
        self.assertTrue(until(lambda: "v_00001.ts" in LOCAL_PLAYLIST.read_text()))
        log = self.directory / "decoder-progress.log"
        log.unlink(missing_ok=True)
        proc = subprocess.Popen([
            "ffmpeg", "-hide_banner", "-nostats", "-loglevel", "error",
            "-protocol_whitelist", "file,crypto,data", "-i", str(LOCAL_PLAYLIST),
            "-map", "0:v:0", "-map", "0:a:0", "-progress", str(log),
            "-f", "null", "-"
        ], stderr=subprocess.PIPE, text=True)
        try:
            def played_five_seconds():
                if not log.exists():
                    return False
                return any(int(x) >= 5_000_000 for x in
                           re.findall(r"out_time_us=(\d+)", log.read_text()))
            self.assertTrue(until(played_five_seconds, 18),
                            "FFmpeg did not advance past old segment safely")
            (self.control / "retire_first").touch()
            old = LOCAL_PLAYLIST.parent / "v_00000.ts"
            self.assertTrue(until(lambda: not old.exists(), 12),
                            "Old decoded segment was not actually unlinked")
            _, err = proc.communicate(timeout=28)
            self.assertEqual(proc.returncode, 0, err)
            recorded = [int(x) for x in re.findall(
                r"out_time_us=(\d+)", log.read_text())]
            self.assertTrue(recorded and max(recorded) >= 11_000_000,
                            "Decoder failed after the old segment was evicted")
            self.assertIn("#EXT-X-MEDIA-SEQUENCE:1", LOCAL_PLAYLIST.read_text())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()

    def test_early_cancel_purges_private_media(self):
        self.assertTrue(until(lambda: "v_00001.ts" in LOCAL_PLAYLIST.read_text()))
        (self.control / "cancel").touch()
        # No ENDLIST should be mistaken for a completed VOD after cancellation.
        self.assertNotIn("#EXT-X-ENDLIST", LOCAL_PLAYLIST.read_text())
        self.assertTrue(LOCAL_PLAYLIST.exists(),
                        "The active session must retain media until decoder closes")


if __name__ == "__main__":
    unittest.main()
