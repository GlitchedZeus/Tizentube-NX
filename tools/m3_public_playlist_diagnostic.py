#!/usr/bin/env python3
"""Privacy-safe anonymous playlist diagnostic for ordinary public videos.

The only emitted details are a public video ID and constant categories.
Never logs CDN/visitor URLs, signed paths, headers, source JSON or bytes.
Results from a GitHub runner are advisory, not Switch network proof.
"""
from __future__ import annotations
import argparse
import json
import re
import urllib.error
import urllib.request

import vision_hls_live_probe as base

TESTED_IDS=("-CuQnpJ4sPg","YJABBgpaNts","0NktpcjsK58")


def classify_manifest(body: bytes,content_type: str,
                      declared_length: str | None) -> str:
    if not body:
        return "MANIFEST_EMPTY"
    if len(body)>base.MAX_PLAYLIST:
        return "MANIFEST_OVERSIZE"
    ct=content_type.split(";",1)[0].strip().lower()
    if ct in ("application/vnd.apple.mpegurl","application/x-mpegurl",
              "audio/mpegurl"):
        mime="HLS_MIME"
    elif ct=="application/octet-stream":
        mime="OCTET_MIME"
    elif ct=="text/plain":
        mime="TEXT_MIME"
    elif ct=="text/html":
        mime="HTML_MIME"
    elif not ct:
        mime="NO_MIME"
    else:
        mime="OTHER_MIME"
    if declared_length is not None:
        if not declared_length.isdecimal():
            return mime+"_INVALID_LENGTH"
        if int(declared_length)!=len(body):
            return mime+"_MISMATCH_LENGTH"
    if body.startswith(b"\xef\xbb\xbf"):
        kind="UTF8_BOM"
    elif not body.startswith(b"#EXTM3U"):
        kind="NON_HLS_BODY"
    elif b"#EXT-X-KEY:" in body or b"#EXT-X-SESSION-KEY:" in body:
        kind="ENCRYPTED"
    elif b"#EXT-X-DATERANGE:" in body or b"#EXT-X-CUE-OUT" in body:
        kind="AD_CUE"
    elif b"#EXT-X-BYTERANGE:" in body or b"#EXT-X-DISCONTINUITY" in body:
        kind="UNSUPPORTED_SEGMENTS"
    elif b"#EXT-X-STREAM-INF:" in body:
        kind="MASTER_AVC_ONLY" if (
            b"avc1." in body and b"mp4a.40." not in body) else "MASTER_OTHER"
    elif b"#EXTINF:" in body:
        kind="MEDIA_CRLF" if b"\r\n" in body else "MEDIA_LF"
    else:
        kind="UNKNOWN_HLS"
    return mime+"_"+kind


def inspect_public_video(video_id: str,visitor: str) -> str:
    stage="PLAYER"
    try:
        request=urllib.request.Request(base.PLAYER_URL,
            data=base.vision_player_body(video_id,visitor),
            headers={
                "Content-Type":"application/json",
                "Accept":"application/json",
                "User-Agent":base.VISION_UA,
                "X-Youtube-Client-Name":"101",
                "X-Youtube-Client-Version":"1.02",
                "X-Goog-Visitor-Id":visitor,
                "Origin":"https://www.youtube.com",
                "Referer":"https://www.youtube.com/",
            },method="POST")
        with urllib.request.build_opener(base.NoRedirect()).open(
                request,timeout=15) as response:
            if response.status!=200:return "PLAYER_STATUS"
            data=response.read(base.MAX_JSON+1)
        uri=base.vision_manifest_url(data,video_id)
        if not uri:
            return base.classify_hls_source_failure(data,video_id)
        if not base.approved_hls(uri):
            return "MANIFEST_URL_POLICY_REJECTED"
        stage="HLS"
        request=urllib.request.Request(uri,headers={
            "Accept":"application/vnd.apple.mpegurl",
            "Accept-Encoding":"identity",
            "User-Agent":base.VISION_UA,
        },method="GET")
        with urllib.request.build_opener(base.NoRedirect()).open(
                request,timeout=15) as response:
            if response.status!=200:return "HLS_STATUS"
            body=response.read(base.MAX_PLAYLIST+1)
            return classify_manifest(body,response.headers.get("Content-Type",""),
                                     response.headers.get("Content-Length"))
    except urllib.error.HTTPError as err:
        return stage+"_HTTP_"+("403" if err.code==403 else
                                "429" if err.code==429 else "OTHER")
    except (ValueError,TypeError,urllib.error.URLError,OSError,TimeoutError):
        return stage+"_TRANSPORT_OR_SOURCE_UNAVAILABLE"


def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--video-id",action="append",required=True)
    args=parser.parse_args()
    if len(args.video_id)>8 or any(not base.VIDEO_ID.fullmatch(x)
                                   for x in args.video_id):
        parser.error("Use at most 8 public normal-video IDs")
    try:
        result=base.get_bytes(base.BOOTSTRAP_URL,headers={
            "Accept":"*/*",
            "Referer":"https://www.youtube.com/sw.js",
            "User-Agent":base.USER_AGENT,
        },cap=base.MAX_JSON)
        visitor=base.bootstrap_visitor(result)
    except (urllib.error.HTTPError,urllib.error.URLError,OSError,ValueError):
        visitor=None
    if not visitor:
        print("PUBLIC_HLS_DIAGNOSTIC=VISITOR_UNAVAILABLE")
        return 0
    for id in args.video_id:
        print("public_id="+id+" category="+inspect_public_video(id,visitor))
    # Never use a single transient network condition to fail fixed host tests.
    return 0


if __name__=="__main__":
    raise SystemExit(main())
