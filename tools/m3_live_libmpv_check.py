#!/usr/bin/env python3
"""Off-console real HLS+AAC libmpv gate. Files only; zero network access.

Takes exactly two private local paths created and removed by the enclosing
live probe. It never prints paths, URLs, player metadata or media bytes.
"""
from __future__ import annotations
import ctypes
import ctypes.util
import math
import pathlib
import subprocess
import sys
import time


class Node(ctypes.Structure):
    pass


class NodeList(ctypes.Structure):
    _fields_=[("num",ctypes.c_int),
              ("values",ctypes.POINTER(Node)),
              ("keys",ctypes.POINTER(ctypes.c_char_p))]


class NodeValue(ctypes.Union):
    _fields_=[("string",ctypes.c_char_p),("int64",ctypes.c_int64),
              ("double",ctypes.c_double),("list",ctypes.POINTER(NodeList))]


Node._fields_=[("u",NodeValue),("format",ctypes.c_int)]


class Event(ctypes.Structure):
    _fields_=[("event_id",ctypes.c_int),("error",ctypes.c_int),
              ("reply_userdata",ctypes.c_ulonglong),
              ("data",ctypes.c_void_p)]


def verify(video: pathlib.Path,audio: pathlib.Path) -> bool:
    libname=ctypes.util.find_library("mpv")
    if not libname or not video.is_file() or not audio.is_file():
        return False
    # This metadata is local, finite and originates from the bounded media
    # download. FFmpeg is never passed a bearer URL or remote playlist.
    try:
        response=subprocess.run([
            "ffprobe","-v","error","-protocol_whitelist","file",
            "-show_entries","format=duration","-of","default=nw=1:nk=1",
            str(video)],capture_output=True,check=True,timeout=20,text=True)
        duration=float(response.stdout.strip())
    except (ValueError,OSError,subprocess.CalledProcessError,
            subprocess.TimeoutExpired):
        return False
    if not math.isfinite(duration) or not 0.5<=duration<=120:
        return False
    root=video.parent
    if audio.parent!=root:
        return False
    (root/"video.m3u8").write_text(
        "#EXTM3U\n#EXT-X-VERSION:7\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n"
        f"#EXT-X-TARGETDURATION:{math.ceil(duration)}\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        f"#EXTINF:{duration:.3f},\n{video.name}\n"
        "#EXT-X-ENDLIST\n")
    (root/"master.m3u8").write_text(
        "#EXTM3U\n#EXT-X-VERSION:7\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1500000\nvideo.m3u8\n")

    lib=ctypes.CDLL(libname)
    lib.mpv_create.restype=ctypes.c_void_p
    lib.mpv_set_option_string.argtypes=[
        ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]
    lib.mpv_set_option_string.restype=ctypes.c_int
    lib.mpv_set_option.argtypes=[
        ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int,ctypes.c_void_p]
    lib.mpv_set_option.restype=ctypes.c_int
    lib.mpv_initialize.argtypes=[ctypes.c_void_p]
    lib.mpv_initialize.restype=ctypes.c_int
    lib.mpv_command.argtypes=[
        ctypes.c_void_p,ctypes.POINTER(ctypes.c_char_p)]
    lib.mpv_command.restype=ctypes.c_int
    lib.mpv_wait_event.argtypes=[ctypes.c_void_p,ctypes.c_double]
    lib.mpv_wait_event.restype=ctypes.POINTER(Event)
    lib.mpv_get_property.argtypes=[
        ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int,ctypes.c_void_p]
    lib.mpv_get_property.restype=ctypes.c_int
    lib.mpv_get_property_string.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
    lib.mpv_get_property_string.restype=ctypes.c_void_p
    lib.mpv_free.argtypes=[ctypes.c_void_p]
    lib.mpv_terminate_destroy.argtypes=[ctypes.c_void_p]

    player=lib.mpv_create()
    if not player:
        return False
    def option(name,value):
        return lib.mpv_set_option_string(
            player,name.encode(),value.encode())>=0
    def property_string(name):
        raw=lib.mpv_get_property_string(player,name.encode())
        if not raw:return ""
        try:
            return ctypes.string_at(raw).decode("utf-8","replace").lower()
        finally:
            lib.mpv_free(raw)
    def selected_audio():
        n=ctypes.c_int64()
        if lib.mpv_get_property(
                player,b"track-list/count",4,ctypes.byref(n))<0:
            return False
        for i in range(min(n.value,32)):
            prefix=f"track-list/{i}/"
            if property_string(prefix+"type")!="audio":
                continue
            selected=ctypes.c_int()
            if lib.mpv_get_property(
                    player,(prefix+"selected").encode(),
                    3,ctypes.byref(selected))>=0 and selected.value:
                return True
        return False
    try:
        if not all(option(k,v) for k,v in [
            ("config","no"),("vo","null"),("ao","null"),
            ("hwdec","no"),("input-default-bindings","no"),
            ("demuxer-lavf-o","protocol_whitelist=file")]):
            return False
        raw_path=str(audio).encode()
        item=Node(NodeValue(string=raw_path),1)
        entries=(Node*1)(item)
        items=NodeList(1,entries,None)
        outer=Node(NodeValue(list=ctypes.pointer(items)),7)
        if lib.mpv_set_option(
                player,b"audio-files",6,ctypes.byref(outer))<0:
            return False
        if lib.mpv_initialize(player)<0:
            return False
        args=(ctypes.c_char_p*4)(
            b"loadfile",str(root/"master.m3u8").encode(),
            b"replace",None)
        if lib.mpv_command(player,args)<0:
            return False
        loaded=False
        deadline=time.monotonic()+14
        while time.monotonic()<deadline:
            event=lib.mpv_wait_event(player,0.2).contents
            if event.event_id==8:
                loaded=True
            if event.event_id==7:
                return False
            if not loaded:
                continue
            video_codec=property_string("video-codec")
            audio_codec=property_string("audio-codec")
            seconds=ctypes.c_double()
            position=lib.mpv_get_property(
                player,b"time-pos",5,ctypes.byref(seconds))>=0
            if (selected_audio() and
                any(k in video_codec for k in ("h264","h.264","avc")) and
                "aac" in audio_codec and position and seconds.value>=0.25):
                return True
        return False
    finally:
        lib.mpv_terminate_destroy(player)


def main():
    if len(sys.argv)!=3:return 2
    try:
        return 0 if verify(pathlib.Path(sys.argv[1]),
                           pathlib.Path(sys.argv[2])) else 1
    except (OSError,ValueError,subprocess.CalledProcessError,
            subprocess.TimeoutExpired):
        return 1


if __name__=="__main__":
    sys.exit(main())
