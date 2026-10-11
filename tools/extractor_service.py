#!/usr/bin/env python3
"""Optional, local-only M3 extraction proof. NOT a public server or Switch API.

Requires an independent current yt-dlp installation. No cookie imports, passwords,
Google account tokens, or arbitrary user URL input. All returned stream URLs are
ephemeral bearer-like media URLs; never write requests/responses to logs.
"""
from __future__ import annotations

import hmac
import json
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

VIDEO_ID_RE = re.compile(r"^[A-Za-z0-9_-]{11}$")
LABEL_RE = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$")
MAX_REQUEST = 256
MAX_RESPONSE = 256 * 1024
MAX_FORMATS = 200


def reviewable_media_url(url: object) -> bool:
    if not isinstance(url, str) or len(url) > 8192 or not url.startswith("https://"):
        return False
    if any(ord(c) <= 32 or ord(c) == 127 for c in url):
        return False
    try:
        p = urlsplit(url)
        host = p.hostname or ""
        if not host.isascii() or not host.endswith(".googlevideo.com"):
            return False
        if p.username is not None or p.password is not None or p.fragment:
            return False
        if p.port not in (None, 443) or p.netloc.endswith("."):
            return False
        if p.path != "/videoplayback":
            return False
        labels = host.lower().split(".")
        return len(labels) >= 3 and all(LABEL_RE.fullmatch(x) for x in labels)
    except (ValueError, AttributeError):
        return False


def video_codec(text: object) -> str:
    if not isinstance(text, str):
        return "unknown"
    if text.startswith(("avc1", "h264")):
        return "h264"
    if text.startswith(("vp9", "vp09")):
        return "vp9"
    if text.startswith(("av01", "av1")):
        return "av1"
    return "unknown"


def audio_codec(text: object) -> str:
    if not isinstance(text, str):
        return "unknown"
    if text.startswith(("mp4a", "aac")):
        return "aac"
    if text.startswith("opus"):
        return "opus"
    return "unknown"


def bounded_int(value: object, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0
    if value < 0 or value > maximum or int(value) != value:
        return 0
    return int(value)


def normalize_info(info: object, requested_id: str) -> dict:
    if not VIDEO_ID_RE.fullmatch(requested_id):
        raise ValueError("invalid video id")
    if not isinstance(info, dict) or info.get("id") != requested_id:
        raise ValueError("upstream identity mismatch")
    page = info.get("webpage_url")
    if isinstance(page, str) and ("/shorts/" in page.lower() or "/reel/" in page.lower()):
        raise ValueError("shorts are forbidden")
    if info.get("_type", "video") not in ("video", None):
        raise ValueError("not a single video")
    source = info.get("formats", [])
    if not isinstance(source, list) or len(source) > MAX_FORMATS:
        raise ValueError("unbounded formats")

    video = []
    audio = []
    for fmt in source:
        if not isinstance(fmt, dict) or not reviewable_media_url(fmt.get("url")):
            continue
        v = video_codec(fmt.get("vcodec"))
        a = audio_codec(fmt.get("acodec"))
        bitrate = bounded_int(fmt.get("tbr", 0), 1_000_000) * 1000
        if v != "unknown":
            height = bounded_int(fmt.get("height", 0), 2160)
            width = bounded_int(fmt.get("width", 0), 4096)
            fps = bounded_int(fmt.get("fps", 0), 120)
            if not width or not height or not fps or not bitrate:
                continue
            dynamic_range = fmt.get("dynamic_range")
            hdr = isinstance(dynamic_range, str) and dynamic_range.upper() not in ("SDR", "")
            video.append({
                "url": fmt["url"],
                "codec": v,
                "muxed_audio": a,
                "width": width,
                "height": height,
                "fps": fps,
                "bit_depth": bounded_int(fmt.get("bit_depth", 8), 32),
                "bitrate": bitrate,
                "hdr": hdr,
                "drm": bool(fmt.get("has_drm", False)),
            })
        elif a != "unknown":
            abr = bounded_int(fmt.get("abr", 0), 1000) * 1000
            audio.append({
                "url": fmt["url"],
                "codec": a,
                "bitrate": abr or bitrate,
                "drm": bool(fmt.get("has_drm", False)),
            })

    duration_s = bounded_int(info.get("duration", 0), 7 * 24 * 60 * 60)
    return {
        "video_id": requested_id,
        "duration_ms": duration_s * 1000,
        "is_live": info.get("live_status") == "is_live",
        "is_short": False,
        "video": video,
        "audio": audio,
    }


def extract_video(video_id: str) -> dict:
    if not VIDEO_ID_RE.fullmatch(video_id):
        raise ValueError("invalid video id")
    try:
        import yt_dlp
    except ImportError as exc:
        raise RuntimeError("yt-dlp is not installed") from exc

    # Quiet, no credential/cookie-file integration, no playlist expansion.
    # Optional current EJS / PO-token providers belong in the yt-dlp environment.
    opts = {
        "quiet": True, "no_warnings": True, "noplaylist": True,
        "skip_download": True, "cachedir": False,
        "ignoreerrors": False, "extract_flat": False,
        "nocheckcertificate": False, "cookiefile": None,
    }
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(
            "https://www.youtube.com/watch?v=" + video_id, download=False)
    return normalize_info(info, video_id)


class Handler(BaseHTTPRequestHandler):
    # No HTTP access log: media URLs are sensitive, even on localhost.
    def log_message(self, fmt: str, *args: object) -> None:
        return

    def _send(self, status: int, payload: dict) -> None:
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        if len(data) > MAX_RESPONSE:
            status, data = 502, b'{"error":"response too large"}'
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        if self.path == "/healthz":
            self._send(200, {"status": "ready", "network": "disabled-for-health"})
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self) -> None:
        if self.path != "/v1/resolve":
            self._send(404, {"error": "not found"})
            return
        expected = self.server.auth_token  # type: ignore[attr-defined]
        actual = self.headers.get("Authorization", "")
        if not hmac.compare_digest(actual, "Bearer " + expected):
            self._send(401, {"error": "unauthorized"})
            return
        raw_length = self.headers.get("Content-Length", "")
        if not raw_length.isdecimal() or not (0 < int(raw_length) <= MAX_REQUEST):
            self._send(413, {"error": "invalid request size"})
            return
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            self._send(415, {"error": "JSON required"})
            return
        try:
            request = json.loads(self.rfile.read(int(raw_length)))
            if not isinstance(request, dict) or set(request) != {"video_id"}:
                raise ValueError("invalid request")
            video_id = request["video_id"]
            if not isinstance(video_id, str) or not VIDEO_ID_RE.fullmatch(video_id):
                raise ValueError("invalid video id")
        except (ValueError, UnicodeDecodeError, TypeError):
            self._send(400, {"error": "invalid request"})
            return
        try:
            data = extract_video(video_id)
        except Exception:
            # Never reflect yt-dlp exceptions: they may contain ephemeral URLs
            # or provider details. Do not log the exception either.
            self._send(502, {"error": "extraction unavailable"})
            return
        self._send(200, data)


def main() -> None:
    secret = os.environ.get("TTNX_EXTRACTOR_TOKEN", "")
    if len(secret) < 32:
        raise SystemExit("Set a random TTNX_EXTRACTOR_TOKEN of at least 32 characters")
    # Loopback-only by design. A Switch-facing interface requires an independent
    # TLS/auth/transport security review and is NOT enabled by this prototype.
    server = ThreadingHTTPServer(("127.0.0.1", 8765), Handler)
    server.auth_token = secret
    try:
        server.serve_forever()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
