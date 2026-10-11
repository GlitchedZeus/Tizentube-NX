#!/usr/bin/env python3
"""Real-world M3 VisionOS guest-HLS source smoke test (never exports URLs).

Intentionally tests the same class of request as native TizenTube NX, not
yt-dlp's WEB client. It cannot establish Nintendo Switch decoder correctness.
GitHub-hosted IPs can be challenged even when an actual console can browse.
No cookies/account/PoToken; no signed media URLs or raw errors are logged.
"""
from __future__ import annotations
import argparse
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

VIDEO_ID=re.compile(r"^[A-Za-z0-9_-]{11}$")
HOST_LABEL=re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$")
BOOTSTRAP_URL="https://www.youtube.com/sw.js_data"
PLAYER_URL="https://www.youtube.com/youtubei/v1/player?prettyPrint=false"
USER_AGENT="Mozilla/5.0 (Nintendo Switch; TizenTube NX) AppleWebKit/537.36 Chrome/120.0 Safari/537.36"
VISION_UA="com.google.ios.youtube/1.02 (RealityDevice17,1; U; CPU visionOS 26_5 like Mac OS X)"
MAX_JSON=4*1024*1024
MAX_PLAYLIST=1024*1024
MAX_SAMPLE=65536


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,request,fp,code,msg,headers,newurl):
        return None


def approved_hls(url: object) -> bool:
    if not isinstance(url,str) or len(url)>8192 or not url.startswith("https://"):
        return False
    if any(ord(c)<33 or ord(c)>126 for c in url) or chr(92) in url:
        return False
    try:
        p=urllib.parse.urlsplit(url)
        host=p.hostname or ""
        if p.username is not None or p.password is not None or p.fragment or p.port not in (None,443):
            return False
        if not host.endswith(".googlevideo.com"):
            return False
        if any(not HOST_LABEL.fullmatch(part) for part in host.split(".")):
            return False
        if not p.path.startswith(("/api/manifest/hls_","/videoplayback")):
            return False
        path=p.path.lower()
        return (".." not in path and "//" not in path and "\\\\" not in path and
                "%2e" not in path and "%2f" not in path)
    except (TypeError,ValueError,AttributeError):
        return False


def get_bytes(url: str, *, headers: dict, cap: int) -> bytes:
    opener=urllib.request.build_opener(NoRedirect())
    req=urllib.request.Request(url,headers=headers,method="GET")
    with opener.open(req,timeout=15) as response:
        if response.status not in (200,206):
            raise ValueError("HTTP_STATUS_UNAVAILABLE")
        data=response.read(cap+1)
        if not data or len(data)>cap:
            raise ValueError("BODY_SIZE_UNAVAILABLE")
        return data


def bootstrap_visitor(payload: bytes) -> str | None:
    if not payload or len(payload)>MAX_JSON:
        return None
    start=payload.find(b"[")
    if start<0:
        return None
    try:
        data=json.loads(payload[start:])
        visitor=data[0][2][0][0][13]
        if not isinstance(visitor,str) or not 1<=len(visitor)<=4096:
            return None
        return visitor
    except (TypeError,ValueError,IndexError,KeyError):
        return None


def vision_player_body(video_id: str,visitor: str) -> bytes:
    if not VIDEO_ID.fullmatch(video_id) or not 1<=len(visitor)<=4096:
        raise ValueError("INVALID_SOURCE_IDENTITY")
    request={
        "context":{"client":{
            "clientName":"VISIONOS",
            "clientVersion":"1.02",
            "deviceMake":"Apple",
            "deviceModel":"RealityDevice17,1",
            "osName":"visionOS",
            "osVersion":"26.5.23O471",
            "hl":"en","gl":"US","visitorData":visitor,
        }},
        "videoId":video_id,
    }
    return json.dumps(request,separators=(",",":")).encode("utf-8")


def vision_manifest_url(payload: bytes,expected_id: str) -> str | None:
    if not payload or len(payload)>MAX_JSON:
        return None
    try:
        root=json.loads(payload)
        if root["playabilityStatus"]["status"]!="OK":
            return None
        meta=root["videoDetails"]
        if meta["videoId"]!=expected_id or meta.get("isShort") or meta.get("isLiveContent"):
            return None
        if not 0<int(meta["lengthSeconds"])<=7*24*3600:
            return None
        url=root["streamingData"]["hlsManifestUrl"]
        return url if approved_hls(url) and "/api/manifest/hls_playlist/" in url else None
    except (TypeError,ValueError,IndexError,KeyError):
        return None


def pick_hls_variant(master: bytes, master_url: str) -> str | None:
    if not master or len(master)>MAX_PLAYLIST:
        return None
    try:
        lines=master.decode("utf-8").splitlines()
    except UnicodeDecodeError:
        return None
    if not lines or lines[0]!="#EXTM3U":
        return None
    found=[]
    for i,line in enumerate(lines[:-1]):
        if not line.startswith("#EXT-X-STREAM-INF:"):
            continue
        meta=line.split(":",1)[1]
        if "avc1." not in meta or "mp4a.40." not in meta:
            continue
        if "RESOLUTION=" not in meta:
            continue
        marker=re.search(r"RESOLUTION=\d+x(\d+)",meta)
        if not marker:
            continue
        height=int(marker.group(1))
        if height>720 or height<144:
            continue
        target=urllib.parse.urljoin(master_url,lines[i+1])
        if approved_hls(target):
            found.append((height,target))
    return max(found,key=lambda item:item[0])[1] if found else None


def first_hls_segment(playlist: bytes,variant_url: str) -> str | None:
    if not playlist or len(playlist)>MAX_PLAYLIST:
        return None
    try:
        lines=playlist.decode("utf-8").splitlines()
    except UnicodeDecodeError:
        return None
    if not lines or lines[0]!="#EXTM3U" or "#EXT-X-ENDLIST" not in lines:
        return None
    for i,line in enumerate(lines[:-1]):
        if line.startswith(("#EXT-X-KEY:","#EXT-X-BYTERANGE:","#EXT-X-CUE-OUT")):
            return None
        if line.startswith("#EXTINF:"):
            target=urllib.parse.urljoin(variant_url,lines[i+1])
            return target if approved_hls(target) else None
    return None


def one_source(video_id: str,visitor: str) -> str:
    try:
        post=urllib.request.Request(
            PLAYER_URL,data=vision_player_body(video_id,visitor),
            headers={
                "Content-Type":"application/json",
                "Accept":"application/json",
                "User-Agent":VISION_UA,
                "X-Youtube-Client-Name":"101",
                "X-Youtube-Client-Version":"1.02",
                "X-Goog-Visitor-Id":visitor,
                "Origin":"https://www.youtube.com",
                "Referer":"https://www.youtube.com/",
            },method="POST")
        opener=urllib.request.build_opener(NoRedirect())
        with opener.open(post,timeout=15) as response:
            if response.status!=200:
                return "PLAYER_HTTP_UNAVAILABLE"
            data=response.read(MAX_JSON+1)
        manifest=vision_manifest_url(data,video_id)
        if not manifest:
            return "VISION_HLS_SOURCE_UNAVAILABLE"
        headers={
            "Accept":"application/vnd.apple.mpegurl",
            "Accept-Encoding":"identity",
            "User-Agent":VISION_UA,
        }
        master=get_bytes(manifest,headers=headers,cap=MAX_PLAYLIST)
        variant=pick_hls_variant(master,manifest)
        if not variant:
            return "HLS_MASTER_NO_COMPATIBLE_AVC"
        media=get_bytes(variant,headers=headers,cap=MAX_PLAYLIST)
        segment=first_hls_segment(media,variant)
        if not segment:
            return "HLS_VOD_SEGMENTS_UNAVAILABLE"
        # Only a bounded real CDN range. Never download the whole clip from CI.
        sample=get_bytes(segment,headers={
            "Range":"bytes=0-65535",
            "Accept-Encoding":"identity",
            "Accept":"application/octet-stream",
        },cap=MAX_SAMPLE)
        if len(sample)<12 or sample.startswith((b"<html",b"<!DOCTYPE",b"#EXTM3U")):
            return "HLS_SEGMENT_NOT_MEDIA"
        return "HLS_SEGMENT_BYTES_OBTAINED"
    except (urllib.error.HTTPError,urllib.error.URLError,OSError,ValueError,TimeoutError):
        return "UPSTREAM_OR_CDN_UNAVAILABLE"


def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--video-id",action="append",required=True)
    args=parser.parse_args()
    ids=args.video_id
    if len(ids)>3 or len(set(ids))!=len(ids) or any(not VIDEO_ID.fullmatch(x) for x in ids):
        parser.error("Use 1–3 unique normal 11-character video IDs")
    try:
        raw=get_bytes(BOOTSTRAP_URL,headers={
            "Accept":"*/*",
            "Referer":"https://www.youtube.com/sw.js",
            "User-Agent":USER_AGENT,
        },cap=MAX_JSON)
        visitor=bootstrap_visitor(raw)
    except (urllib.error.HTTPError,urllib.error.URLError,OSError,ValueError):
        visitor=None
    if not visitor:
        print("VISITOR_BOOTSTRAP_UNAVAILABLE")
        return 1
    for video_id in ids:
        result=one_source(video_id,visitor)
        print(f"source={video_id} stage={result}")
        if result=="HLS_SEGMENT_BYTES_OBTAINED":
            print("M3_VISION_HLS_LIVE_REFERENCE=PASS")
            return 0
    print("M3_VISION_HLS_LIVE_REFERENCE=FAIL")
    return 1


if __name__=="__main__":
    sys.exit(main())
