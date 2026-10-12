#!/usr/bin/env python3
"""Host-only libmpv proof: native H264 MP4 plus externally attached AAC."""
import ctypes
import ctypes.util
import pathlib
import sys
import time

from m3_live_libmpv_check import Event, Node, NodeList, NodeValue

def run(video: pathlib.Path,audio: pathlib.Path)->bool:
    library=ctypes.util.find_library("mpv")
    if not library or not video.is_file() or not audio.is_file():return False
    mpv=ctypes.CDLL(library)
    mpv.mpv_create.restype=ctypes.c_void_p
    mpv.mpv_set_option_string.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p]
    mpv.mpv_set_option_string.restype=ctypes.c_int
    mpv.mpv_set_option.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int,ctypes.c_void_p]
    mpv.mpv_set_option.restype=ctypes.c_int
    mpv.mpv_initialize.argtypes=[ctypes.c_void_p]
    mpv.mpv_initialize.restype=ctypes.c_int
    mpv.mpv_command.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_char_p)]
    mpv.mpv_command.restype=ctypes.c_int
    mpv.mpv_wait_event.argtypes=[ctypes.c_void_p,ctypes.c_double]
    mpv.mpv_wait_event.restype=ctypes.POINTER(Event)
    mpv.mpv_get_property.argtypes=[
        ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int,ctypes.c_void_p]
    mpv.mpv_get_property.restype=ctypes.c_int
    mpv.mpv_terminate_destroy.argtypes=[ctypes.c_void_p]
    handle=mpv.mpv_create()
    if not handle:return False
    try:
        for name,value in [("config","no"),("vo","null"),("ao","null"),
                            ("hwdec","no"),("pause","yes"),
                            ("demuxer-lavf-o","protocol_whitelist=file")]:
            if mpv.mpv_set_option_string(handle,name.encode(),value.encode())<0:
                return False
        raw=str(audio).encode()
        item=Node(NodeValue(string=raw),1)
        items=(Node*1)(item)
        listing=NodeList(1,items,None)
        arr=Node(NodeValue(list=ctypes.pointer(listing)),7)
        if mpv.mpv_set_option(handle,b"audio-files",6,
                              ctypes.byref(arr))<0:return False
        if mpv.mpv_initialize(handle)<0:return False
        def command(*fields):
            cmd=(ctypes.c_char_p*(len(fields)+1))(
                *(str(x).encode() for x in fields),None)
            return mpv.mpv_command(handle,cmd)
        if command("loadfile",str(video),"replace")<0:return False
        loaded=False
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            e=mpv.mpv_wait_event(handle,0.2).contents
            if e.event_id==7:return False
            if e.event_id==8:loaded=True;break
        if not loaded:return False
        count=ctypes.c_int64()
        if mpv.mpv_get_property(handle,b"track-list/count",4,
                                ctypes.byref(count))<0 or count.value<2:return False
        # Selected audio must exist before claiming success.
        audio_selected=False
        for i in range(min(count.value,16)):
            prefix=f"track-list/{i}/"
            sel=ctypes.c_int()
            rc=mpv.mpv_get_property(handle,(prefix+"selected").encode(),3,
                                    ctypes.byref(sel))
            if rc>=0 and sel.value:
                # Host proof also requires ffprobe to identify one video
                # and one AAC audio source in the calling integration test.
                audio_selected=True
        if not audio_selected or command("seek","30","absolute")<0:return False
        if command("cycle","pause")<0:return False
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            event=mpv.mpv_wait_event(handle,0.2).contents
            if event.event_id==7:return False
            position=ctypes.c_double()
            if mpv.mpv_get_property(handle,b"time-pos",5,
                                    ctypes.byref(position))>=0 and (
                                    position.value>=29):return True
        return False
    finally:
        mpv.mpv_terminate_destroy(handle)

if __name__=="__main__":
    if len(sys.argv)!=3:raise SystemExit(2)
    raise SystemExit(0 if run(pathlib.Path(sys.argv[1]),
                              pathlib.Path(sys.argv[2])) else 1)
