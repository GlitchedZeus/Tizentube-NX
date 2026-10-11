"""Copyright-free FFmpeg integration gate for locally cached HLS + AAC.

This checks actual demux and decode, not signatures alone. The Switch mpv
option and physical A/V synchronization must still be verified on hardware.
"""
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True,
                          text=True, timeout=90)


def probe(path):
    return json.loads(run("ffprobe", "-v", "error", "-show_entries",
                          "stream=codec_name,codec_type:format=duration",
                          "-of", "json", str(path)).stdout)


class LocalAvDemuxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for cmd in ("ffmpeg", "ffprobe"):
            if not shutil.which(cmd):
                raise RuntimeError(cmd + " required for CI media compatibility")
        cls.tmp = tempfile.TemporaryDirectory(prefix="ttnx-local-av-")
        cls.root = pathlib.Path(cls.tmp.name)
        cls.ready = cls.root / "ready"
        cls.ready.mkdir()
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24",
            "-t", "9", "-an", "-c:v", "libx264", "-preset", "ultrafast",
            "-pix_fmt", "yuv420p", "-g", "72", "-sc_threshold", "0",
            "-f", "hls", "-hls_segment_type", "mpegts", "-hls_time", "3",
            "-hls_list_size", "0", "-hls_playlist_type", "vod",
            "-hls_segment_filename", str(cls.ready / "v_%05d.ts"),
            str(cls.root / "upstream.m3u8"))
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000",
            "-t", "9", "-c:a", "aac", "-b:a", "96k",
            "-movflags", "+faststart", str(cls.ready / "external_audio.m4a"))
        incoming = (cls.root / "upstream.m3u8").read_text()
        durations = [float(m.group(1)) for m in
                     re.finditer(r"^#EXTINF:([0-9.]+),", incoming, re.M)]
        assert 2 <= len(durations) <= 8
        assert abs(sum(durations) - 9) < .1
        segs = "".join("#EXTINF:{:.3f},\nv_{:05d}.ts\n".format(d, i)
                       for i, d in enumerate(durations))
        (cls.ready / "video.m3u8").write_text(
            "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-PLAYLIST-TYPE:VOD\n"
            "#EXT-X-TARGETDURATION:{}\n#EXT-X-MEDIA-SEQUENCE:0\n".format(
                int(max(durations) + .999)) + segs + "#EXT-X-ENDLIST\n")
        (cls.ready / "master.m3u8").write_text(
            "#EXTM3U\n#EXT-X-VERSION:7\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=1500000\nvideo.m3u8\n")
        (cls.ready / "audio_mode.txt").write_text("external\n")
        cls.expected_aac_bytes = (cls.ready / "external_audio.m4a").stat().st_size
        (cls.ready / "external_audio.bytes").write_text(
            str(cls.expected_aac_bytes) + "\n")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_real_local_video_plus_external_aac_decode(self):
        video = probe(self.ready / "master.m3u8")
        audio = probe(self.ready / "external_audio.m4a")
        self.assertEqual([(s["codec_name"], s["codec_type"])
                          for s in video["streams"]], [("h264", "video")])
        self.assertIn(("aac", "audio"), [(s["codec_name"], s["codec_type"])
                                         for s in audio["streams"]])
        self.assertLess(abs(float(video["format"]["duration"]) -
                            float(audio["format"]["duration"])), 1.0)
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-xerror",
            "-protocol_whitelist", "file",
            "-i", str(self.ready / "master.m3u8"),
            "-i", str(self.ready / "external_audio.m4a"),
            "-map", "0:v:0", "-map", "1:a:0", "-f", "null", "-")

    def test_local_playlist_is_video_only_without_external_audio(self):
        stream = probe(self.ready / "master.m3u8")
        self.assertFalse(any(s["codec_type"] == "audio"
                             for s in stream["streams"]))
        for name in ("master.m3u8", "video.m3u8"):
            data = (self.ready / name).read_text()
            self.assertNotIn("http:", data)
            self.assertNotIn("https:", data)
            self.assertNotIn("googlevideo", data)
            self.assertNotIn("../", data)
        self.assertEqual((self.ready / "audio_mode.txt").read_text(),
                         "external\n")

    def test_native_mpv_uses_real_list_option_not_cli_only_alias(self):
        source = (pathlib.Path(__file__).resolve().parents[1] /
                  "switch/source/m3_player_activity.cpp").read_text()
        self.assertIn('required("audio-files-append",', source)
        self.assertNotIn('required("audio-file",', source)

    def test_audio_missing_and_torn_files_are_not_complete(self):
        path = self.ready / "external_audio.m4a"
        original = path.read_bytes()
        try:
            path.unlink()
            self.assertFalse(path.exists())
            path.write_bytes(original[:256])
            self.assertNotEqual(path.stat().st_size,
                                int((self.ready / "external_audio.bytes")
                                    .read_text().strip()))
            with self.assertRaises(subprocess.CalledProcessError):
                run("ffmpeg", "-hide_banner", "-loglevel", "error",
                    "-xerror", "-i", str(path), "-map", "0:a:0",
                    "-f", "null", "-")
        finally:
            path.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
