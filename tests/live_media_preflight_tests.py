"""Strict offline regressions for optional real-video reference preflight."""
from __future__ import annotations
from email.message import Message
import io
import pathlib
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import live_video_preflight as live


class FakeReply:
    def __init__(self, status=206, body=None, media_type="video/mp4",
                 content_range="bytes 0-31/32"):
        self.status = status
        self.body = body if body is not None else b"\0\0\0\x18ftypisom" + b"A" * 20
        self.headers = Message()
        self.headers["Content-Type"] = media_type
        self.headers["Content-Length"] = str(len(self.body))
        self.headers["Content-Range"] = content_range

    def read(self, count: int):
        return self.body[:count]

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False


class FakeOpener:
    def __init__(self, response: FakeReply):
        self.response = response
        self.requests = []

    def open(self, request, timeout):
        self.requests.append(request)
        return self.response


class SafetyTests(unittest.TestCase):
    URL = "https://r1.googlevideo.com/videoplayback?sig=PRIVATE_SIGNED_URL"

    def test_live_range_success_and_no_cookie(self):
        opener = FakeOpener(FakeReply())
        with patch.object(live.urllib.request, "build_opener", return_value=opener):
            ok, stage = live.verify_media_bytes(self.URL)
        self.assertTrue(ok)
        self.assertEqual(stage, "VERIFIED_MP4_RANGE")
        self.assertEqual(opener.requests[0].headers["Range"], live.RANGE_HEADER)
        self.assertNotIn("Cookie", opener.requests[0].headers)
        self.assertNotIn("Authorization", opener.requests[0].headers)

    def test_reject_unsafe_host_before_open(self):
        opener = FakeOpener(FakeReply())
        with patch.object(live.urllib.request, "build_opener", return_value=opener):
            ok, stage = live.verify_media_bytes(
                "https://accounts.nintendo.com/videoplayback")
        self.assertFalse(ok)
        self.assertEqual(stage, "URL_POLICY_REJECTED")
        self.assertEqual(opener.requests, [])

    def test_unbounded_200_rejected(self):
        opener = FakeOpener(FakeReply(status=200))
        with patch.object(live.urllib.request, "build_opener", return_value=opener):
            ok, stage = live.verify_media_bytes(self.URL)
        self.assertFalse(ok)
        self.assertEqual(stage, "NOT_PARTIAL_CONTENT")

    def test_html_payload_rejected(self):
        opener = FakeOpener(FakeReply(body=b"<html>" + b"A"*26,
                                      media_type="text/html"))
        with patch.object(live.urllib.request, "build_opener", return_value=opener):
            ok, stage = live.verify_media_bytes(self.URL)
        self.assertFalse(ok)
        self.assertEqual(stage, "UNEXPECTED_MEDIA_TYPE")

    def test_truncated_and_wrong_range_rejected(self):
        for response in (
                FakeReply(content_range="bytes 3-34/500"),
                FakeReply(content_range="bytes 0-31/500"), # truncated before actual EOF
                FakeReply(body=b"A" * 31),
        ):
            opener = FakeOpener(response)
            with patch.object(live.urllib.request, "build_opener",
                              return_value=opener):
                ok, _ = live.verify_media_bytes(self.URL)
            self.assertFalse(ok)

    def test_never_log_opaque_upstream_exception(self):
        opener = FakeOpener(FakeReply())
        def failed(request, timeout):
            raise OSError(f"opaque bearer leakage {request.full_url}")
        opener.open = failed
        with patch.object(live.urllib.request, "build_opener", return_value=opener):
            ok, stage = live.verify_media_bytes(self.URL)
        self.assertFalse(ok)
        self.assertEqual(stage, "CDN_RANGE_UNAVAILABLE")
        self.assertNotIn("PRIVATE_SIGNED_URL", stage)

    def test_candidate_requires_muxed_supported_mp4(self):
        fmt = {
            "url": self.URL, "ext": "mp4", "protocol": "https",
            "vcodec": "avc1.64001F", "acodec": "mp4a.40.2",
            "height": 360, "fps": 30, "tbr": 600,
        }
        self.assertEqual(live.candidate_formats({"formats": [fmt]}), [fmt])
        for key, value in (
                ("acodec", "none"),
                ("vcodec", "vp09.00.51"),
                ("url", "http://r1.googlevideo.com/videoplayback"),
                ("ext", "webm"),
                ("height", 2160),
                ("fps", 120),
                ("has_drm", True),
        ):
            candidate = dict(fmt, **{key: value})
            self.assertEqual(live.candidate_formats({"formats": [candidate]}),
                             [], f"unsafe {key} passed")


if __name__ == "__main__":
    unittest.main()
