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
import os
import pathlib
import subprocess
import tempfile
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
MAX_DECODE_MEDIA=16*1024*1024


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


def get_media_sample(url: str, headers: dict) -> bytes:
    # Diagnostics only: a CDN may return HTTP 200 and the *entire* HLS
    # segment even when asked for Range 0-65535. Never reject a valid
    # media response solely because the full file is larger than a sample.
    # Read and retain at most 64 KiB; the native player has its own strict
    # 16 MiB full-segment cap, MIME and length validation.
    if not approved_hls(url):
        raise ValueError("URL_POLICY_REJECTED")
    opener=urllib.request.build_opener(NoRedirect())
    request=urllib.request.Request(url,headers=headers,method="GET")
    with opener.open(request,timeout=15) as response:
        if response.status not in (200,206):
            raise ValueError("HTTP_STATUS_UNAVAILABLE")
        if response.headers.get("Content-Encoding","identity").lower()!="identity":
            raise ValueError("UNSUPPORTED_CONTENT_ENCODING")
        media=response.read(MAX_SAMPLE)
        if len(media)<12:
            raise ValueError("MEDIA_SAMPLE_TOO_SHORT")
        return media


def identify_media_container(sample: bytes) -> str:
    # Only report a fixed container label. Never output media bytes or paths.
    if len(sample)>=376 and sample[0]==0x47 and sample[188]==0x47:
        return "MPEG_TS"
    if len(sample)>=12 and sample[4:8] in (
            b"ftyp",b"styp",b"moof",b"moov",b"mdat"):
        return "ISOBMFF"
    if len(sample)>=3 and sample[0]==0xff and (sample[1]&0xf0)==0xf0:
        return "ADTS_AUDIO"
    return "UNRECOGNIZED"


def detect_ts_stream_types(sample: bytes) -> set[int]:
    """Read MPEG-TS PAT+PMT stream type numbers from a 64 KiB sample.

    We do not log packet contents/PIDs or raw video data. A missing/truncated
    PSI table fails the format proof instead of guessing the codecs.
    """
    if identify_media_container(sample)!="MPEG_TS":
        return set()
    pmt_pid=None
    types:set[int]=set()
    for offset in range(0,len(sample)-187,188):
        packet=sample[offset:offset+188]
        if packet[0]!=0x47:
            return set()
        pid=((packet[1]&0x1f)<<8)|packet[2]
        pusi=(packet[1]&0x40)!=0
        afc=(packet[3]>>4)&3
        if afc not in (1,3):
            continue
        start=4
        if afc==3:
            if start>=188:
                continue
            start+=1+packet[start]
        if not pusi or start+1>=188:
            continue
        pointer=packet[start]
        start+=1+pointer
        if start+3>188:
            continue
        payload=packet[start:]
        section_len=((payload[1]&0x0f)<<8)|payload[2]
        if section_len<9 or section_len>1021 or len(payload)<3+section_len:
            continue
        section=payload[:3+section_len]
        if pid==0 and section[0]==0 and pmt_pid is None:
            end=len(section)-4
            for i in range(8,end-3,4):
                program=(section[i]<<8)|section[i+1]
                if program:
                    pmt_pid=((section[i+2]&0x1f)<<8)|section[i+3]
                    break
        elif pid==pmt_pid and section[0]==2:
            if len(section)<16:
                continue
            prog_info=((section[10]&0x0f)<<8)|section[11]
            pos=12+prog_info
            end=len(section)-4
            while pos+5<=end:
                types.add(section[pos])
                es_info=((section[pos+3]&0x0f)<<8)|section[pos+4]
                pos+=5+es_info
            return types
    return types


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
        return url if approved_hls(url) and (
            "/api/manifest/hls_playlist/" in url or
            "/api/manifest/hls_variant/" in url) else None
    except (TypeError,ValueError,IndexError,KeyError):
        return None


def hls_url_rejection_category(url: object) -> str:
    # Exact privacy-preserving source diagnostics, not signed URL logging.
    if not isinstance(url,str):
        return "HLS_URL_TYPE_UNSUPPORTED"
    if len(url)>8192:
        return "HLS_URL_LENGTH_UNSUPPORTED"
    if not url.startswith("https://"):
        return "HLS_URL_SCHEME_UNSUPPORTED"
    if chr(92) in url or any(ord(c)<33 or ord(c)>126 for c in url):
        return "HLS_URL_INVALID_CHARACTERS"
    try:
        parsed=urllib.parse.urlsplit(url)
        if not (parsed.hostname or "").endswith(".googlevideo.com"):
            return "HLS_URL_HOST_POLICY_UNSUPPORTED"
        if parsed.username or parsed.password or parsed.fragment or parsed.port not in (None,443):
            return "HLS_URL_AUTHORITY_UNSUPPORTED"
        if not parsed.path.startswith("/api/manifest/hls_"):
            return "HLS_URL_PATH_FAMILY_UNSUPPORTED"
        if not parsed.path.startswith("/api/manifest/hls_playlist/"):
            # Fixed categories only: no signed path segment, video ID,
            # visitor identifier, query parameter, or host is printed.
            if parsed.path.startswith("/api/manifest/hls_playlist"):
                return "HLS_MASTER_PATH_WITHOUT_SLASH"
            if parsed.path.startswith("/api/manifest/hls_variant"):
                return "HLS_MASTER_DIRECT_VARIANT_ROUTE"
            return "HLS_MASTER_OTHER_HLS_ROUTE"
        if any(part.lower() in ("%2f","%2e","%5c") for part in
               re.findall(r"%[0-9A-Fa-f]{2}",parsed.path)):
            return "HLS_URL_ESCAPED_PATH_UNSUPPORTED"
        return "HLS_URL_OTHER_STRUCTURAL_REJECTION"
    except (TypeError,ValueError,AttributeError):
        return "HLS_URL_PARSE_FAILED"


def classify_hls_source_failure(payload: bytes, expected_id: str) -> str:
    # Enumerated stage only. Never output upstream messages, client tokens,
    # visitorData, arbitrary HTML/JSON, or signed Googlevideo URLs.
    try:
        if len(payload)>MAX_JSON:
            return "PLAYER_RESPONSE_OVERSIZE"
        obj=json.loads(payload)
        status=obj.get("playabilityStatus",{}).get("status")
        if status!="OK":
            return "PLAYER_PLAYABILITY_BLOCKED"
        details=obj.get("videoDetails",{})
        if details.get("videoId")!=expected_id:
            return "PLAYER_IDENTITY_MISMATCH"
        if details.get("isShort") or details.get("isLiveContent"):
            return "SHORTS_OR_LIVE_BLOCKED"
        streaming=obj.get("streamingData")
        if not isinstance(streaming,dict):
            return "NO_STREAMING_DATA"
        url=streaming.get("hlsManifestUrl")
        if not url:
            return "HLS_NOT_OFFERED_BY_PLAYER"
        return hls_url_rejection_category(url)
    except (TypeError,ValueError,AttributeError,KeyError):
        return "PLAYER_RESPONSE_UNUSABLE"


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


def select_direct_aac_audio(player_json: bytes) -> str | None:
    try:
        response=json.loads(player_json)
        formats=response.get("streamingData",{}).get("adaptiveFormats",[])
        if not isinstance(formats,list):
            return None
        reviewed=[]
        for fmt in formats[:200]:
            if not isinstance(fmt,dict):
                continue
            mime=fmt.get("mimeType","")
            url=fmt.get("url")
            if (not isinstance(mime,str) or
                not mime.startswith("audio/mp4") or "mp4a.40." not in mime or
                fmt.get("drmFamilies") or fmt.get("signatureCipher") or
                fmt.get("cipher") or
                not isinstance(url,str) or not approved_hls(url) or
                "/videoplayback" not in url):
                continue
            bitrate=fmt.get("bitrate",0)
            if not isinstance(bitrate,(int,float)) or not 0<bitrate<=512000:
                continue
            reviewed.append((bitrate,url))
        # This picks a real linked AAC media URL, but does not imply
        # full duration or audio/video synchronization has been verified.
        return max(reviewed,key=lambda item:item[0])[1] if reviewed else None
    except (TypeError,ValueError,KeyError,AttributeError):
        return None


def summarize_audio_offerings(player_json: bytes) -> str:
    """Fixed category only; do not reveal URLs, signature keys or IDs."""
    try:
        info=json.loads(player_json)
        stream=info.get("streamingData",{})
        formats=stream.get("adaptiveFormats",[])
        if not isinstance(formats,list):
            return "AUDIO_FORMATS_MALFORMED"
        direct=0
        cipher=0
        media_kind=0
        for fmt in formats[:200]:
            if not isinstance(fmt,dict):
                continue
            mime=fmt.get("mimeType","")
            if not isinstance(mime,str) or not mime.startswith("audio/"):
                continue
            if "mp4a.40." not in mime and "opus" not in mime:
                continue
            media_kind+=1
            url=fmt.get("url")
            if isinstance(url,str) and approved_hls(url) and "/videoplayback" in url:
                direct+=1
            if fmt.get("signatureCipher") or fmt.get("cipher"):
                cipher+=1
        if direct:
            return "AAC_OR_OPUS_DIRECT_AUDIO_URL_OFFERED"
        if cipher:
            return "AUDIO_SIGNATURE_DECIPHER_REQUIRED"
        if media_kind:
            return "AUDIO_FORMATS_NO_DIRECT_URL"
        return "AUDIO_FORMATS_ABSENT"
    except (ValueError,TypeError,AttributeError):
        return "AUDIO_FORMATS_RESPONSE_MALFORMED"


def classify_vision_audio_candidates(payload: bytes) -> str:
    # Report a constant source-class enum only; never any signed URL,
    # signatureCipher, CDN host, visitor cookie, or upstream messages.
    try:
        info=json.loads(payload)
        streaming=info.get("streamingData",{})
        formats=streaming.get("adaptiveFormats",[])
        if not isinstance(formats,list):
            return "AUDIO_FORMAT_LIST_INVALID"
        has_aac=False
        has_cipher=False
        for f in formats[:200]:
            if not isinstance(f,dict):
                continue
            mime=f.get("mimeType","")
            if not isinstance(mime,str) or not mime.startswith("audio/mp4") or "mp4a.40." not in mime:
                continue
            has_aac=True
            url=f.get("url")
            if approved_hls(url):
                return "AUDIO_AAC_SIGNED_CDN_URL_AVAILABLE"
            if isinstance(f.get("signatureCipher"),str):
                has_cipher=True
        if has_cipher:
            return "AUDIO_AAC_CIPHER_ONLY"
        return "AUDIO_AAC_NO_USABLE_URL" if has_aac else "AUDIO_AAC_NOT_LISTED"
    except (TypeError,KeyError,ValueError,AttributeError):
        return "AUDIO_LIST_UNAVAILABLE"



def get_complete_media(url: str) -> bytes:
    """One full, bounded signed media object. No redirects, no URL logging.

    This intentionally exercises complete audio and video bytes in LIVE CI
    without allowing mpv/ffmpeg any network access. A partial 206 must span
    the entire object; a mere first chunk cannot count as decoded media.
    """
    if not approved_hls(url):
        raise ValueError("DECODE_MEDIA_URL_REJECTED")
    request=urllib.request.Request(url,method="GET",headers={
        "Accept":"application/octet-stream",
        "Accept-Encoding":"identity",
    })
    with urllib.request.build_opener(NoRedirect()).open(request,timeout=25) as response:
        if response.status not in (200,206):
            raise ValueError("DECODE_MEDIA_HTTP_UNAVAILABLE")
        if response.headers.get("Content-Encoding","identity").lower()!="identity":
            raise ValueError("DECODE_MEDIA_ENCODING_REJECTED")
        contents=response.read(MAX_DECODE_MEDIA+1)
        if not 376<=len(contents)<=MAX_DECODE_MEDIA:
            raise ValueError("DECODE_MEDIA_SIZE_REJECTED")
        length=response.headers.get("Content-Length")
        if length is not None:
            if not length.isdigit() or int(length)!=len(contents):
                raise ValueError("DECODE_MEDIA_TRUNCATED")
        if response.status==206:
            value=response.headers.get("Content-Range","")
            match=re.fullmatch(r"bytes 0-(\d+)/(\d+)",value)
            if not match or int(match.group(1))+1!=int(match.group(2)) or (
                    int(match.group(2))!=len(contents)):
                raise ValueError("DECODE_MEDIA_PARTIAL")
        return contents



def get_complete_aac_ranges(url: str) -> bytes:
    """Reproduce the native 1 MiB HTTP 206 AAC download contract.

    This deliberately does NOT use a full un-ranged GET: Googlevideo may
    allow one and reject the other. Every response must be contiguous and
    exactly bounded by its verified Content-Range / total resource length.
    """
    if not approved_hls(url) or "/videoplayback" not in url:
        raise ValueError("DECODE_AUDIO_URL_REJECTED")
    result=bytearray()
    total=None
    opener=urllib.request.build_opener(NoRedirect())
    while total is None or len(result)<total:
        start=len(result)
        end=start+1048575
        request=urllib.request.Request(url,method="GET",headers={
            "Range":f"bytes={start}-{end}",
            "Accept":"application/octet-stream",
            "Accept-Encoding":"identity",
        })
        with opener.open(request,timeout=25) as response:
            if response.status!=206:
                raise ValueError("DECODE_AUDIO_NOT_PARTIAL")
            if response.headers.get("Content-Encoding","identity").lower()!="identity":
                raise ValueError("DECODE_AUDIO_ENCODING")
            cr=response.headers.get("Content-Range","")
            match=re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)",cr)
            if not match:
                raise ValueError("DECODE_AUDIO_RANGE_INVALID")
            actual_start,actual_end,declared=map(int,match.groups())
            if declared<1024 or declared>MAX_DECODE_MEDIA or (
                    total is not None and declared!=total):
                raise ValueError("DECODE_AUDIO_TOTAL_INVALID")
            total=declared
            expected_end=min(end,total-1)
            if actual_start!=start or actual_end!=expected_end:
                raise ValueError("DECODE_AUDIO_DISCONTINUITY")
            chunk=response.read(1048576+1)
            if len(chunk)!=expected_end-start+1:
                raise ValueError("DECODE_AUDIO_TRUNCATED")
            length=response.headers.get("Content-Length")
            if length is not None and (
                    not length.isdigit() or int(length)!=len(chunk)):
                raise ValueError("DECODE_AUDIO_LENGTH_INVALID")
            result.extend(chunk)
    return bytes(result)


def decode_real_av_pair(video: bytes,audio: bytes) -> bool:
    """Decode actual A/V samples with file-only FFmpeg and discard all bytes.

    Never pass a bearer URL, ID, visitor token, or network protocol to
    ffmpeg/ffprobe. Decoder errors are not echoed (they may be untrusted).
    """
    if identify_media_container(video)!="MPEG_TS" or (
            0x1b not in detect_ts_stream_types(video[:MAX_SAMPLE])):
        return False
    if identify_media_container(audio)!="ISOBMFF" or audio[4:8]!=b"ftyp":
        return False
    with tempfile.TemporaryDirectory(prefix="ttnx-live-av-") as directory:
        root=pathlib.Path(directory)
        video_path=root/"video.ts"
        audio_path=root/"audio.m4a"
        video_path.write_bytes(video)
        audio_path.write_bytes(audio)
        def inspect(path: pathlib.Path) -> list[tuple[str,str]]:
            run=subprocess.run([
                "ffprobe","-v","error",
                "-protocol_whitelist","file",
                "-show_entries","stream=codec_name,codec_type",
                "-of","json",str(path),
            ],capture_output=True,timeout=25,check=True)
            info=json.loads(run.stdout)
            return [(s.get("codec_name",""),s.get("codec_type",""))
                    for s in info.get("streams",[])]
        if ("h264","video") not in inspect(video_path) or (
                "aac","audio") not in inspect(audio_path):
            return False
        # This is the same real C++ TS / MP4 parser linked into the Switch
        # build, not a Python port or merely an FFmpeg codec declaration.
        validator=os.environ.get("TTNX_M3_NATIVE_MEDIA_CHECK")
        if not validator:
            return False
        subprocess.run([validator,str(video_path),str(audio_path)],
                       capture_output=True,check=True,timeout=30)
        # Independent decoding catches broken packets even if stream metadata
        # is present; combined mapping verifies the two actual files can be
        # opened in one local process without remote access.
        for args in [
            ["-i",str(video_path),"-map","0:v:0",
             "-frames:v","24","-f","null","-"],
            ["-i",str(audio_path),"-map","0:a:0",
             "-t","2","-f","null","-"],
            ["-i",str(video_path),"-i",str(audio_path),
             "-map","0:v:0","-map","1:a:0","-t","3","-f","null","-"],
        ]:
            subprocess.run(["ffmpeg","-nostdin","-hide_banner","-loglevel",
                            "error","-xerror","-protocol_whitelist","file",
                            *args],capture_output=True,check=True,timeout=35)
        print("M3_NATIVE_MEDIA_PARSERS=PASS")
        return True


def one_source(video_id: str,visitor: str,decode_real: bool=False) -> str:
    stage="PLAYER"
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
        audio_state=summarize_audio_offerings(data)
        print("M3_AUDIO_SOURCE_CLASS="+classify_vision_audio_candidates(data))
        manifest=vision_manifest_url(data,video_id)
        if not manifest:
            return classify_hls_source_failure(data,video_id)
        headers={
            "Accept":"application/vnd.apple.mpegurl",
            "Accept-Encoding":"identity",
            "User-Agent":VISION_UA,
        }
        stage="HLS_PLAYLIST"
        manifest_bytes=get_bytes(manifest,headers=headers,cap=MAX_PLAYLIST)
        # YouTube may return the media playlist directly as hls_variant,
        # without a top-level master/variant resolution step.
        if b"#EXT-X-STREAM-INF:" in manifest_bytes:
            variant=pick_hls_variant(manifest_bytes,manifest)
            if not variant:
                return "HLS_MASTER_NO_COMPATIBLE_AVC"
            stage="HLS_VARIANT"
            media=get_bytes(variant,headers=headers,cap=MAX_PLAYLIST)
        elif b"#EXTINF:" in manifest_bytes:
            variant=manifest
            media=manifest_bytes
        else:
            return "HLS_PLAYLIST_FORMAT_UNSUPPORTED"
        segment=first_hls_segment(media,variant)
        if not segment:
            return "HLS_VOD_SEGMENTS_UNAVAILABLE"
        # Only a bounded real CDN range. Never download the whole clip from CI.
        stage="HLS_SEGMENT"
        sample=get_media_sample(segment,headers={
            "Range":"bytes=0-65535",
            "Accept-Encoding":"identity",
            "Accept":"application/octet-stream",
        })
        if len(sample)<12 or sample.startswith((b"<html",b"<!DOCTYPE",b"#EXTM3U")):
            return "HLS_SEGMENT_NOT_MEDIA"
        container=identify_media_container(sample)
        if container=="MPEG_TS":
            types=detect_ts_stream_types(sample)
            if 0x1b in types and any(t in types for t in (0x0f,0x11)):
                return "HLS_MPEG_TS_H264_AAC_STREAMS_VERIFIED"
            if 0x1b in types:
                audio=select_direct_aac_audio(data)
                if not audio:
                    return "HLS_H264_AUDIO_SOURCE_MISSING"
                stage="AAC_AUDIO_SAMPLE"
                sample=get_media_sample(audio,headers={
                    "Range":"bytes=0-65535",
                    "Accept-Encoding":"identity",
                    "Accept":"application/octet-stream",
                })
                if identify_media_container(sample)!="ISOBMFF":
                    return "HLS_H264_AUDIO_CONTAINER_UNVERIFIED"
                if decode_real:
                    stage="REAL_AV_DOWNLOAD"
                    full_video=get_complete_media(segment)
                    full_audio=get_complete_aac_ranges(audio)
                    stage="REAL_AV_DECODE"
                    try:
                        if decode_real_av_pair(full_video,full_audio):
                            return "HLS_REAL_H264_AAC_DECODE_VERIFIED"
                    except (subprocess.CalledProcessError,
                            subprocess.TimeoutExpired,ValueError,OSError):
                        return "REAL_AV_DECODE_REJECTED"
                    return "REAL_AV_CODEC_REJECTED"
                return "HLS_H264_AND_SEPARATE_AAC_MP4_BYTES_VERIFIED"
            return "HLS_MPEG_TS_CODEC_UNVERIFIED"
        return "HLS_SEGMENT_"+container+"_BYTES_OBTAINED"
    except urllib.error.HTTPError as exc:
        if exc.code==403:
            return stage+"_HTTP_403"
        if exc.code==429:
            return stage+"_HTTP_429"
        return stage+"_HTTP_OTHER"
    except ValueError as exc:
        if str(exc) in {"BODY_SIZE_UNAVAILABLE","MEDIA_SAMPLE_TOO_SHORT"}:
            return stage+"_SAMPLE_SIZE_UNAVAILABLE"
        if str(exc)=="UNSUPPORTED_CONTENT_ENCODING":
            return stage+"_ENCODING_UNSUPPORTED"
        return stage+"_TRANSPORT_UNAVAILABLE"
    except (urllib.error.URLError,OSError,TimeoutError):
        return stage+"_TRANSPORT_UNAVAILABLE"


def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--video-id",action="append",required=True)
    parser.add_argument("--decode-real-av",action="store_true",
                        help="Require bounded real FFmpeg H.264/AAC decoding")
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
        result=one_source(video_id,visitor,decode_real=args.decode_real_av)
        print(f"source={video_id} stage={result}")
        acceptable={"HLS_REAL_H264_AAC_DECODE_VERIFIED"} if args.decode_real_av else {
            "HLS_MPEG_TS_H264_AAC_STREAMS_VERIFIED",
            "HLS_H264_AND_SEPARATE_AAC_MP4_BYTES_VERIFIED",
            "HLS_SEGMENT_ISOBMFF_BYTES_OBTAINED"}
        if result in acceptable:
            print("M3_VISION_HLS_LIVE_REFERENCE=PASS")
            if args.decode_real_av:
                print("M3_REAL_AV_DECODE=PASS")
            return 0
    print("M3_VISION_HLS_LIVE_REFERENCE=FAIL")
    return 1


if __name__=="__main__":
    sys.exit(main())
