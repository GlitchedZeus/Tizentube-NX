"""Source-level native M4 HTTPS safety/compile contract.

This cannot substitute for devkitA64 compilation. It prevents the observed
undeclared media_plane regression and accidental use of browsing-only DNS
or whole-response fallback in the streaming-only Horizon SSL function.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
NET = (ROOT / "switch/source/libnx_http_client.cpp").read_text()
WORKFLOW = (ROOT / ".github/workflows/m3-media-transport-build.yml").read_text()

class M4NativeSslStreamContract(unittest.TestCase):
    @staticmethod
    def streaming_body():
        return NET.split("net::HttpResult perform_single_streamed_media_request(",1)[1].split(
            "\n}\n#endif",1
        )[0]

    def test_streaming_helper_explicit_media_dns_gate(self):
        body=self.streaming_body()
        self.assertIn("tcp_connect_allowed_host(parsed_url.host, timeout_ms, true)",body)
        self.assertNotIn("timeout_ms, media_plane)",body)
        # Retain independent certificate-chain, SNI, hostname and date gates.
        for token in ("SslVerifyOption_PeerCa", "SslVerifyOption_HostName",
                      "SslVerifyOption_DateCheck", "sslConnectionSetHostName",
                      "sslConnectionDoHandshake"):
            self.assertIn(token,body)

    def test_no_unbounded_body_or_redirect_fallback(self):
        body=self.streaming_body()
        self.assertIn("net::Http1MediaStream parser(admit)",body)
        self.assertIn("parser.feed(",body)
        self.assertIn("parser.finish()",body)
        self.assertIn("net::Http1MediaStream::kReceiveChunk",body)
        self.assertNotIn("perform_single_request(",body)
        self.assertNotIn("perform(",body)
        self.assertNotIn("redirect_url(",body)
        self.assertNotIn("std::vector<char>",body)
        self.assertNotIn("response.body.append",body)

    def test_pre_dns_signed_media_policy_and_flags(self):
        block=NET.split("LibnxHlsSegmentTransport::stream_segment(",1)[1]
        self.assertIn("is_reviewable_hls_child_url(url)",block)
        self.assertIn("parsed->port!=443",block)
        self.assertIn("perform_single_streamed_media_request(",block)
        self.assertIn("if(bytes.size()>32*1024 || !emit(bytes))return false;",block)
        self.assertIn("byte_count>16*1024*1024",block)
        self.assertIn("TTNX_M4_ROLLING_STREAM=1",WORKFLOW)
        self.assertIn("build-m4-rolling/libnx_http_client.o",WORKFLOW)

    def test_legacy_browser_transport_keeps_separate_gate(self):
        legacy=NET.split("net::HttpResult perform_single_request(",1)[1].split(
            "net::HttpResult perform_single_streamed_media_request(",1)[0]
        self.assertIn("bool media_plane",legacy)
        self.assertIn("tcp_connect_allowed_host(parsed_url.host, timeout_ms, media_plane)",legacy)

if __name__ == "__main__":
    unittest.main()
