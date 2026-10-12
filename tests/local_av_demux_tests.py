"""Copyright-free FFmpeg integration gate for locally cached HLS + AAC.

This checks actual demux and decode, not signatures alone. The Switch mpv
option and physical A/V synchronization must still be verified on hardware.
"""
import ctypes
import ctypes.util
import json
import pathlib
import re
import shutil
import subprocess
import time
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
        self.assertIn('mpv_set_option(handle_,"audio-files",MPV_FORMAT_NODE', source)
        self.assertNotIn('required("audio-files-append",', source)
        self.assertNotIn('required("audio-file",', source)

    def test_host_libmpv_selects_external_aac_and_controls(self):
        """Exercise libmpv API, not just the FFmpeg command-line demuxer."""
        library = ctypes.util.find_library("mpv")
        self.assertIsNotNone(library, "CI must install libmpv2")
        mpv = ctypes.CDLL(library)

        class Event(ctypes.Structure):
            _fields_ = [("event_id", ctypes.c_int),
                        ("error", ctypes.c_int),
                        ("reply_userdata", ctypes.c_ulonglong),
                        ("data", ctypes.c_void_p)]

        mpv.mpv_create.restype = ctypes.c_void_p
        mpv.mpv_set_option_string.argtypes = [ctypes.c_void_p,
                                                ctypes.c_char_p, ctypes.c_char_p]
        mpv.mpv_set_option_string.restype = ctypes.c_int
        mpv.mpv_set_option.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                       ctypes.c_int, ctypes.c_void_p]
        mpv.mpv_set_option.restype = ctypes.c_int
        mpv.mpv_initialize.argtypes = [ctypes.c_void_p]
        mpv.mpv_initialize.restype = ctypes.c_int
        mpv.mpv_command.argtypes = [ctypes.c_void_p,
                                     ctypes.POINTER(ctypes.c_char_p)]
        mpv.mpv_command.restype = ctypes.c_int
        mpv.mpv_wait_event.argtypes = [ctypes.c_void_p, ctypes.c_double]
        mpv.mpv_wait_event.restype = ctypes.POINTER(Event)
        mpv.mpv_get_property.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.c_int, ctypes.c_void_p]
        mpv.mpv_get_property.restype = ctypes.c_int
        mpv.mpv_get_property_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        mpv.mpv_get_property_string.restype = ctypes.c_void_p
        mpv.mpv_free.argtypes = [ctypes.c_void_p]
        mpv.mpv_terminate_destroy.argtypes = [ctypes.c_void_p]

        class Node(ctypes.Structure):
            pass
        class NodeList(ctypes.Structure):
            _fields_ = [("num", ctypes.c_int),
                        ("values", ctypes.POINTER(Node)),
                        ("keys", ctypes.POINTER(ctypes.c_char_p))]
        class NodeValue(ctypes.Union):
            _fields_ = [("string", ctypes.c_char_p),
                        ("flag", ctypes.c_int),
                        ("int64", ctypes.c_int64),
                        ("double", ctypes.c_double),
                        ("list", ctypes.POINTER(NodeList))]
        Node._fields_ = [("u", NodeValue), ("format", ctypes.c_int)]

        handle = mpv.mpv_create()
        self.assertTrue(handle, "mpv_create must return a real handle")
        def opt(key, value):
            return mpv.mpv_set_option_string(handle, key.encode(),
                                             value.encode())
        def cmd(*words):
            argv = (ctypes.c_char_p * (len(words) + 1))()
            for i, word in enumerate(words):
                argv[i] = str(word).encode()
            return mpv.mpv_command(handle, argv)
        def prop(name):
            raw = mpv.mpv_get_property_string(handle, name.encode())
            if not raw:
                return None
            try:
                return ctypes.string_at(raw).decode("utf-8", "replace")
            finally:
                mpv.mpv_free(raw)

        try:
            for key, value in [
                ("config", "no"), ("vo", "null"), ("ao", "null"),
                ("hwdec", "no"), ("pause", "yes"),
                ("demuxer-lavf-o", "protocol_whitelist=file")
            ]:
                self.assertGreaterEqual(opt(key, value), 0,
                    f"libmpv rejected required option {key}")
            raw_path = str(self.ready / "external_audio.m4a").encode()
            item = Node(NodeValue(string=raw_path), 1)
            item_array = (Node * 1)(item)
            node_list = NodeList(1, item_array, None)
            node = Node(NodeValue(list=ctypes.pointer(node_list)), 7)
            self.assertGreaterEqual(mpv.mpv_set_option(
                handle, b"audio-files", 6, ctypes.byref(node)), 0,
                "real libmpv must support typed audio-files NODE_ARRAY")
            self.assertGreaterEqual(mpv.mpv_initialize(handle), 0,
                                    "host libmpv initialization")
            self.assertGreaterEqual(cmd("loadfile",
                                         str(self.ready / "master.m3u8"), "replace"), 0)
            loaded = False
            deadline = time.monotonic() + 18
            while time.monotonic() < deadline:
                event = mpv.mpv_wait_event(handle, 0.25).contents
                if event.event_id == 8:  # MPV_EVENT_FILE_LOADED
                    loaded = True
                    break
                if event.event_id == 7:  # MPV_EVENT_END_FILE before load
                    self.fail("libmpv ended HLS before FILE_LOADED")
            self.assertTrue(loaded, "libmpv must demux local HLS playlist")
            count = ctypes.c_int64()
            self.assertGreaterEqual(mpv.mpv_get_property(
                handle, b"track-list/count", 4, ctypes.byref(count)), 0)
            selected_audio = False
            video_seen = False
            for index in range(min(count.value, 24)):
                prefix = f"track-list/{index}/"
                kind = prop(prefix + "type")
                if kind == "video":
                    video_seen = True
                if kind == "audio":
                    selected = ctypes.c_int()
                    ok = mpv.mpv_get_property(
                        handle, (prefix + "selected").encode(), 3,
                        ctypes.byref(selected))
                    selected_audio |= ok >= 0 and selected.value != 0
            self.assertTrue(video_seen, "H.264 video track must exist")
            self.assertTrue(selected_audio,
                "real libmpv must attach and select the external AAC")
            self.assertGreaterEqual(cmd("seek", "4", "absolute"), 0,
                                    "libmpv must support local HLS seeking")
            self.assertGreaterEqual(cmd("cycle", "pause"), 0,
                                    "libmpv must support pause/resume")
            self.assertGreaterEqual(cmd("stop"), 0,
                                    "libmpv must support stop")
            self.assertGreaterEqual(cmd("loadfile",
                                         str(self.ready / "master.m3u8"), "replace"), 0,
                                    "libmpv must allow replay after stop")
        finally:
            mpv.mpv_terminate_destroy(handle)

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
