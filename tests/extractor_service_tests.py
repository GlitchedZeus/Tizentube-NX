"""Offline unit and loopback integration tests for the M3 resolver prototype."""
from __future__ import annotations

import json
import pathlib
import sys
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import extractor_service as service


class NormalizationTests(unittest.TestCase):
    def setUp(self):
        self.id = "IzJ7R4EnYmI"
        self.media = "https://r1---sn-test.googlevideo.com/videoplayback?x=signed"
        self.info = {
            "id": self.id,
            "duration": 120,
            "formats": [
                {"url": self.media, "vcodec": "avc1.64001f", "acodec": "none",
                 "width": 1280, "height": 720, "fps": 30, "tbr": 1800},
                {"url": self.media, "vcodec": "none", "acodec": "mp4a.40.2",
                 "abr": 128},
            ],
        }

    def test_normalized_adaptive_formats(self):
        data = service.normalize_info(self.info, self.id)
        self.assertEqual(data["video_id"], self.id)
        self.assertEqual(data["duration_ms"], 120000)
        self.assertEqual(data["video"][0]["codec"], "h264")
        self.assertEqual(data["audio"][0]["codec"], "aac")
        self.assertEqual(data["audio"][0]["bitrate"], 128000)

    def test_no_shorts(self):
        self.info["webpage_url"] = "https://www.youtube.com/shorts/" + self.id
        with self.assertRaises(ValueError):
            service.normalize_info(self.info, self.id)

    def test_no_identity_substitution(self):
        self.info["id"] = "Different11"
        with self.assertRaises(ValueError):
            service.normalize_info(self.info, self.id)

    def test_bounded_formats(self):
        self.info["formats"] = [self.info["formats"][0]] * 201
        with self.assertRaises(ValueError):
            service.normalize_info(self.info, self.id)

    def test_no_arbitrary_media_url(self):
        self.info["formats"][0]["url"] = "https://accounts.nintendo.com/videoplayback"
        self.info["formats"][1]["url"] = "https://malicious.example/videoplayback"
        data = service.normalize_info(self.info, self.id)
        self.assertEqual(data["video"], [])
        self.assertEqual(data["audio"], [])

    def test_unsupported_dragon_codec(self):
        self.info["formats"][0]["vcodec"] = "av01.0"
        data = service.normalize_info(self.info, self.id)
        self.assertEqual(data["video"][0]["codec"], "av1")
        # The Switch native selector independently refuses AV1 for initial M3.

    def test_url_hostile_inputs(self):
        bad = [
            "https://googlevideo.com/videoplayback",
            "https://evilgooglevideo.com/videoplayback",
            "https://r.googlevideo.com.evil.test/videoplayback",
            "https://r.googlevideo.com@accounts.nintendo.com/videoplayback",
            "https://evil@r.googlevideo.com/videoplayback",
            "http://r.googlevideo.com/videoplayback",
            "https://127.0.0.1/videoplayback",
            "https://r.googlevideo.com:444/videoplayback",
            "https://r.googlevideo.com/notvideo",
            "https://r.googlevideo.com/videoplayback#secret",
            "https://r.googlevideo.com/videoplayback\nmalicious",
        ]
        for value in bad:
            with self.subTest(value=value):
                self.assertFalse(service.reviewable_media_url(value))
        self.assertTrue(service.reviewable_media_url(self.media))

    def test_invalid_id_never_reaches_extraction(self):
        for vid in ["", "123", "https://foo", "x" * 12, "bad id!!!!!"]:
            with self.subTest(vid=vid):
                with self.assertRaises(ValueError):
                    service.normalize_info(self.info, vid)


class LoopbackHandlerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = service.ThreadingHTTPServer(("127.0.0.1", 0), service.Handler)
        cls.server.auth_token = "a" * 40
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.endpoint = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join(timeout=5)
        cls.server.server_close()

    def request(self, body: bytes, token: str = "a" * 40, path="/v1/resolve",
                content_type="application/json"):
        req = Request(
            self.endpoint + path, data=body, method="POST",
            headers={"Authorization": "Bearer " + token, "Content-Type": content_type})
        try:
            with urlopen(req, timeout=5) as result:
                return result.status, json.load(result), result.headers
        except HTTPError as result:
            return result.code, json.load(result), result.headers

    def test_health_endpoint(self):
        with urlopen(self.endpoint + "/healthz", timeout=5) as result:
            self.assertEqual(result.status, 200)
            self.assertEqual(result.headers["Cache-Control"], "no-store")

    def test_unauthenticated_rejected(self):
        code, payload, _ = self.request(b'{"video_id":"IzJ7R4EnYmI"}', token="bad")
        self.assertEqual(code, 401)
        self.assertNotIn("video_id", payload)

    def test_bad_input_rejected_before_extraction(self):
        for body in [b"{}", b"[]", b'{"video_id":"../etc/passwd"}',
                     b'{"video_id":"IzJ7R4EnYmI","url":"https://evil"}',
                     b"not-json", b"x" * 257]:
            with self.subTest(body=body[:20]):
                with patch.object(service, "extract_video") as extractor:
                    code, _, _ = self.request(body)
                    self.assertIn(code, (400, 413))
                    extractor.assert_not_called()

    def test_authenticated_resolver_dispatch(self):
        with patch.object(service, "extract_video",
                          return_value={"video_id": "IzJ7R4EnYmI", "video": []}) as resolve:
            code, payload, headers = self.request(b'{"video_id":"IzJ7R4EnYmI"}')
            self.assertEqual(code, 200)
            self.assertEqual(payload["video_id"], "IzJ7R4EnYmI")
            self.assertEqual(headers["Cache-Control"], "no-store")
            resolve.assert_called_once_with("IzJ7R4EnYmI")

    def test_errors_do_not_leak_tokenized_stream_urls(self):
        with patch.object(service, "extract_video",
                          side_effect=RuntimeError("secret?token=do-not-leak")):
            code, payload, _ = self.request(b'{"video_id":"IzJ7R4EnYmI"}')
            self.assertEqual(code, 502)
            self.assertNotIn("secret", json.dumps(payload))
            self.assertNotIn("token=", json.dumps(payload))


if __name__ == "__main__":
    unittest.main()
