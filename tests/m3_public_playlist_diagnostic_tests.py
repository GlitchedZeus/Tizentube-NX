from __future__ import annotations
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from m3_public_playlist_diagnostic import classify_manifest

class PublicPlaylistClassification(unittest.TestCase):
    def test_case_insensitive_playlist_mime(self):
        self.assertEqual(
            classify_manifest(
                b"#EXTM3U\r\n#EXTINF:4.0,\r\nx\r\n#EXT-X-ENDLIST\r\n",
                "Application/X-MpegURL; charset=UTF-8",None),
            "HLS_MIME_MEDIA_CRLF")
    def test_safe_diagnostic_categories(self):
        self.assertEqual(
            classify_manifest(b"<html>blocked</html>","text/html","20"),
            "HTML_MIME_MISMATCH_LENGTH")
        self.assertEqual(
            classify_manifest(
                b"#EXTM3U\n#EXT-X-STREAM-INF:CODECS=\"avc1.4d401e\"\n",
                "application/vnd.apple.mpegurl",None),
            "HLS_MIME_MASTER_AVC_ONLY")
        self.assertEqual(classify_manifest(
            b"#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n",
            "application/vnd.apple.mpegurl",None),
            "HLS_MIME_ENCRYPTED")
        self.assertEqual(classify_manifest(
            b"not a playlist","text/html",None),
            "HTML_MIME_NON_HLS_BODY")

if __name__=="__main__":
    unittest.main()
