"""Offline safety checks for the network-isolated visionOS/HLS live proof gate."""
from __future__ import annotations
import json
import shutil
import subprocess
import tempfile
import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
import vision_hls_live_probe as probe

ID="2kD9v7Q3vec"
MASTER="https://manifest.googlevideo.com/api/manifest/hls_playlist/test"
VARIANT="https://manifest.googlevideo.com/api/manifest/hls_variant/720.m3u8"

class ProofContracts(unittest.TestCase):
    def test_vision_request_uses_visitor_not_credentials(self):
        data=json.loads(probe.vision_player_body(ID,"safe-visitor"))
        self.assertEqual(data["videoId"],ID)
        self.assertEqual(data["context"]["client"]["clientName"],"VISIONOS")
        self.assertEqual(data["context"]["client"]["visitorData"],"safe-visitor")
        self.assertNotIn("serviceIntegrityDimensions",data)
        self.assertNotIn("adSignalsInfo",data)

    def test_bootstrap_only_accepts_correct_device_array(self):
        data=[[None,None,[[["X"]*13+["visitor-data"]]]]]
        self.assertEqual(probe.bootstrap_visitor(b")]}\\n"+json.dumps(data).encode()),
                         "visitor-data")
        self.assertIsNone(probe.bootstrap_visitor(b"not-json"))
        self.assertIsNone(probe.bootstrap_visitor(b"x"*(probe.MAX_JSON+1)))

    def test_player_identity_restricted_and_hls_host_reviewed(self):
        obj={
            "playabilityStatus":{"status":"OK"},
            "videoDetails":{"videoId":ID,"lengthSeconds":"180","isShort":False,
                            "isLiveContent":False},
            "streamingData":{"hlsManifestUrl":MASTER},
        }
        self.assertEqual(probe.vision_manifest_url(json.dumps(obj).encode(),ID),MASTER)
        obj["playabilityStatus"]["status"]="LOGIN_REQUIRED"
        self.assertIsNone(probe.vision_manifest_url(json.dumps(obj).encode(),ID))
        obj["playabilityStatus"]["status"]="OK"
        obj["streamingData"]["hlsManifestUrl"]="https://accounts.nintendo.com/api/manifest/hls_playlist/x"
        self.assertIsNone(probe.vision_manifest_url(json.dumps(obj).encode(),ID))

    def test_bad_destinations_rejected(self):
        for url in [
            "http://manifest.googlevideo.com/api/manifest/hls_playlist/x",
            "https://accounts.nintendo.com/api/manifest/hls_playlist/x",
            "https://manifest.googlevideo.com.evil.test/api/manifest/hls_playlist/x",
            "https://evil@manifest.googlevideo.com/api/manifest/hls_playlist/x",
            "https://manifest.googlevideo.com:444/api/manifest/hls_playlist/x",
            "https://manifest.googlevideo.com/api/manifest/hls_variant/%2e%2e/x",
            "https://manifest.googlevideo.com/api/manifest/hls_variant/x#secret",
            "https://manifest.googlevideo.com/api/manifest/hls_variant/x\\nsecret",
        ]:
            with self.subTest(url=url):
                self.assertFalse(probe.approved_hls(url))
        self.assertTrue(probe.approved_hls(MASTER))

    def test_real_decode_refuses_fake_media_without_network(self):
        self.assertFalse(probe.decode_real_av_pair(
            b"<html>not video</html>",b"not-audio"))
        self.assertFalse(probe.decode_real_av_pair(
            b"\x47"+b"x"*400,b"\x00\x00\x00\x18ftypisom"+b"x"*512))

    def test_real_media_download_policy_blocks_unreviewed_hosts(self):
        for value in ["https://accounts.nintendo.com/videoplayback",
                      "http://r1.googlevideo.com/videoplayback",
                      "https://r1.googlevideo.com:80/videoplayback"]:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    probe.get_complete_media(value)

    def test_native_audio_range_reconstruction_and_bounds(self):
        source="https://r1.googlevideo.com/videoplayback?itag=140"
        audio=b"A"*(1048576+40)
        seen=[]
        class Response:
            def __init__(self,start,end):
                actual_end=min(end,len(audio)-1)
                self.status=206
                self.headers={
                    "Content-Range":f"bytes {start}-{actual_end}/{len(audio)}",
                    "Content-Length":str(actual_end-start+1)}
                self.chunk=audio[start:actual_end+1]
            def __enter__(self): return self
            def __exit__(self,*_): return None
            def read(self,count): return self.chunk[:count]
        class Opener:
            def open(self,request,timeout):
                pair=request.get_header("Range").removeprefix("bytes=").split("-")
                start,end=map(int,pair)
                seen.append((start,end))
                return Response(start,end)
        with mock.patch.object(probe.urllib.request,"build_opener",
                               return_value=Opener()):
            self.assertEqual(probe.get_complete_aac_ranges(source),audio)
        self.assertEqual(seen,[(0,1048575),(1048576,2097151)])

    def test_partial_aac_range_is_rejected(self):
        source="https://r1.googlevideo.com/videoplayback?itag=140"
        class BrokenResponse:
            status=206
            headers={"Content-Range":"bytes 1-100/2048",
                     "Content-Length":"100"}
            def __enter__(self):return self
            def __exit__(self,*_):return None
            def read(self,count):return b"A"*100
        with mock.patch.object(probe.urllib.request,"build_opener") as factory:
            factory.return_value.open.return_value=BrokenResponse()
            with self.assertRaises(ValueError):
                probe.get_complete_aac_ranges(source)

    def test_hls_master_and_segments_checked(self):
        master=(
            '#EXTM3U\n'
            '#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=640x360,'
            'CODECS="avc1.64001f,mp4a.40.2"\n'
            +VARIANT+'\n'
        ).encode()
        self.assertEqual(probe.pick_hls_variant(master,MASTER),VARIANT)
        self.assertIsNone(probe.pick_hls_variant(
            master.replace(b"avc1.64001f",b"av01.0.05"),MASTER))
        self.assertIsNone(probe.pick_hls_variant(
            master.replace(b"manifest.googlevideo.com",
                           b"accounts.nintendo.com"),MASTER))
        media=(
            '#EXTM3U\n#EXTINF:6.000,\n'
            'https://r1.googlevideo.com/videoplayback?segment=1\n'
            '#EXT-X-ENDLIST\n'
        ).encode()
        self.assertEqual(probe.first_hls_segment(media,VARIANT),
                         'https://r1.googlevideo.com/videoplayback?segment=1')
        self.assertIsNone(probe.first_hls_segment(
            media.replace(b'#EXT-X-ENDLIST',b'#EXT-X-KEY:METHOD=AES-128'),VARIANT))

if __name__=="__main__":
    unittest.main()
