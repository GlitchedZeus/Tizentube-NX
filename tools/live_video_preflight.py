#!/usr/bin/env python3
"""Live external extraction preflight, never a Switch authorization or player.

This intentionally exercises REAL public YouTube responses in CI. All media
URLs are transient bearer-like secrets and must never be logged/artifacted.
Running this script is opt-in; the default host test suite remains offline.
Success means an upstream yt-dlp+Deno reference path currently produces a
progressive H264/AAC MP4, not that the C++ Switch extractor can do so.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request

from extractor_service import reviewable_media_url, VIDEO_ID_RE

MAX_RANGE_BYTES = 65536
MAX_VIDEO_BYTES = 512 * 1024 * 1024
RANGE_HEADER = f"bytes=0-{MAX_RANGE_BYTES - 1}"
CONTENT_RANGE = re.compile(r"^bytes 0-(\d+)/(\d+)$")


@dataclass(frozen=True)
class Result:
    source_id: str
    stage: str
    ok: bool


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        raise urllib.error.HTTPError(
            request.full_url, code, "redirect disabled", headers, fp
        )


def candidate_formats(info: dict) -> list[dict]:
    """Only non-DRM, non-live, self-contained H264/AAC direct MP4."""
    if not isinstance(info, dict) or not isinstance(info.get("formats"), list):
        return []
    eligible: list[dict] = []
    for fmt in info["formats"][:500]:
        if not isinstance(fmt, dict):
            continue
        url = fmt.get("url")
        if not reviewable_media_url(url):
            continue
        if not str(fmt.get("vcodec", "")).startswith(("avc1.", "h264")):
            continue
        if not str(fmt.get("acodec", "")).startswith(("mp4a.", "aac")):
            continue
        if fmt.get("has_drm") or fmt.get("protocol") not in (None, "https"):
            continue
        if fmt.get("ext") != "mp4":
            continue
        height, fps = fmt.get("height"), fmt.get("fps")
        if not isinstance(height, (int, float)) or not 0 < height <= 720:
            continue
        if not isinstance(fps, (int, float)) or not 0 < fps <= 30:
            continue
        size = fmt.get("filesize") or fmt.get("filesize_approx") or 0
        if isinstance(size, (int, float)) and size > MAX_VIDEO_BYTES:
            continue
        eligible.append(fmt)
    # Prefer smaller, fast-to-probe streams, not the biggest file.
    return sorted(eligible, key=lambda f: (f.get("height", 9999), f.get("tbr") or 999999))


def verify_media_bytes(url: str, *, timeout: float = 12.0) -> tuple[bool, str]:
    if not reviewable_media_url(url):
        return False, "URL_POLICY_REJECTED"
    # No urllib request may follow a redirect to another host, even to another
    # Googlevideo address. Never send cookies, auth, or custom headers.
    opener = urllib.request.build_opener(NoRedirect())
    request = urllib.request.Request(url, headers={
        "Range": RANGE_HEADER,
        "Accept-Encoding": "identity",
        "Accept": "application/octet-stream",
        "User-Agent": "TizenTube-NX-M3-Reference-Preflight",
    })
    try:
        with opener.open(request, timeout=timeout) as result:
            if result.status != 206:
                return False, "NOT_PARTIAL_CONTENT"
            mime = result.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
            if mime not in {"video/mp4", "application/octet-stream", "binary/octet-stream"}:
                return False, "UNEXPECTED_MEDIA_TYPE"
            if result.headers.get("Content-Encoding", "identity").lower() != "identity":
                return False, "COMPRESSED_RANGE_REJECTED"
            lengths = result.headers.get_all("Content-Length") or []
            ranges = result.headers.get_all("Content-Range") or []
            if len(lengths) != 1 or len(ranges) != 1:
                return False, "AMBIGUOUS_RANGE_HEADERS"
            match = CONTENT_RANGE.fullmatch(ranges[0])
            if not match:
                return False, "MALFORMED_RANGE"
            end, total = map(int, match.groups())
            if (total <= end or total > MAX_VIDEO_BYTES or end >= MAX_RANGE_BYTES
                    or end != min(MAX_RANGE_BYTES - 1, total - 1)):
                # A 206 response may be short only when the actual resource
                # ends: a partial reply before EOF would corrupt the cache.
                return False, "INVALID_MEDIA_SIZE"
            expected = end + 1
            if int(lengths[0]) != expected:
                return False, "RANGE_LENGTH_MISMATCH"
            # Cap reads even for a malicious server ignoring the Range.
            body = result.read(MAX_RANGE_BYTES + 1)
            if len(body) != expected or len(body) > MAX_RANGE_BYTES:
                return False, "TRUNCATED_OR_OVERSIZED_RESPONSE"
            if len(body) < 12 or body[4:8] != b"ftyp":
                return False, "NOT_MP4_CONTAINER"
            return True, "VERIFIED_MP4_RANGE"
    except (urllib.error.HTTPError, urllib.error.URLError, socket.timeout,
            ValueError, OSError):
        # Never serialize upstream exception: it can contain signed URLs.
        return False, "CDN_RANGE_UNAVAILABLE"


def run_one(video_id: str) -> Result:
    try:
        import yt_dlp
    except ImportError:
        return Result(video_id, "REFERENCE_DEPENDENCY_MISSING", False)
    opts = {
        "quiet": True,
        "no_warnings": True,
        "noplaylist": True,
        "skip_download": True,
        "ignoreerrors": False,
        "cachedir": False,
        "cookiefile": None,
        "nocheckcertificate": False,
        "socket_timeout": 12,
        "retries": 1,
        "extractor_retries": 1,
    }
    try:
        with yt_dlp.YoutubeDL(opts) as downloader:
            info = downloader.extract_info(
                "https://www.youtube.com/watch?v=" + video_id, download=False)
    except Exception:
        return Result(video_id, "REFERENCE_EXTRACTION_UNAVAILABLE", False)
    if not isinstance(info, dict) or info.get("id") != video_id:
        return Result(video_id, "INVALID_IDENTITY", False)
    web_url = str(info.get("webpage_url") or "")
    if "/shorts/" in web_url.lower() or info.get("is_live") or info.get("live_status") == "is_live":
        return Result(video_id, "SHORTS_OR_LIVE_REJECTED", False)
    formats = candidate_formats(info)
    if not formats:
        adaptive = bool(info.get("formats"))
        return Result(video_id, "ADAPTIVE_OR_CHALLENGE_REQUIRED" if adaptive
                      else "NO_FORMATS", False)
    # Limit CDN attempts per source; do not hammer YouTube in CI.
    for fmt in formats[:2]:
        ok, stage = verify_media_bytes(fmt["url"])
        if ok:
            return Result(video_id, stage, True)
    return Result(video_id, "CDN_RANGE_UNAVAILABLE", False)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--video-id", action="append", required=True)
    p.add_argument("--minimum-verified", type=int, default=1)
    args = p.parse_args()
    if len(args.video_id) > 4 or len(set(args.video_id)) != len(args.video_id) or not all(
            VIDEO_ID_RE.fullmatch(v) for v in args.video_id):
        p.error("Use 1–4 unique canonical 11-character normal video IDs.")
    print("M3 real YouTube reference preflight: yt-dlp + JS runtime (NOT Switch).",
          flush=True)
    results = [run_one(v) for v in args.video_id]
    for item in results:
        print(f"source={item.source_id} result={item.stage} verified={item.ok}", flush=True)
    verified = sum(x.ok for x in results)
    print(f"VERIFIED_MP4_SOURCES={verified}/{len(results)}", flush=True)
    return 0 if verified >= args.minimum_verified else 1


if __name__ == "__main__":
    sys.exit(main())
