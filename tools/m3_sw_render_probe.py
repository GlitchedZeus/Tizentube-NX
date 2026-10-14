#!/usr/bin/env python3
"""Actual local H.264->libmpv SW callback->RGB frame proof.

No network, signed media, UI, X11 or OpenGL; detects audio-only/black
SW-render regressions that a "track selected" test cannot detect.
"""
import ctypes as c
import ctypes.util
import pathlib
import sys
import time


class Param(c.Structure):
    _fields_ = [("type", c.c_int), ("data", c.c_void_p)]


def verify(path: pathlib.Path) -> bool:
    if not path.is_file() or path.stat().st_size < 1024:
        return False
    library=ctypes_name=ctypes.util.find_library("mpv")
    if not library:
        return False
    mpv=c.CDLL(ctypes_name)
    mpv.mpv_create.restype=c.c_void_p
    mpv.mpv_set_option_string.argtypes=[c.c_void_p,c.c_char_p,c.c_char_p]
    mpv.mpv_set_option_string.restype=c.c_int
    mpv.mpv_initialize.argtypes=[c.c_void_p]
    mpv.mpv_initialize.restype=c.c_int
    mpv.mpv_command.argtypes=[c.c_void_p,c.POINTER(c.c_char_p)]
    mpv.mpv_command.restype=c.c_int
    mpv.mpv_render_context_create.argtypes=[
        c.POINTER(c.c_void_p),c.c_void_p,c.POINTER(Param)]
    mpv.mpv_render_context_create.restype=c.c_int
    callback_type=c.CFUNCTYPE(None,c.c_void_p)
    mpv.mpv_render_context_set_update_callback.argtypes=[
        c.c_void_p,callback_type,c.c_void_p]
    mpv.mpv_render_context_update.argtypes=[c.c_void_p]
    mpv.mpv_render_context_update.restype=c.c_uint64
    mpv.mpv_render_context_render.argtypes=[c.c_void_p,c.POINTER(Param)]
    mpv.mpv_render_context_render.restype=c.c_int
    mpv.mpv_render_context_report_swap.argtypes=[c.c_void_p]
    mpv.mpv_render_context_free.argtypes=[c.c_void_p]
    mpv.mpv_terminate_destroy.argtypes=[c.c_void_p]
    handle=mpv.mpv_create()
    if not handle:return False
    ctx=c.c_void_p()
    cb=None
    try:
        for key,value in (
            ("config","no"),("vo","libmpv"),("hwdec","no"),
            ("ao","null"),("demuxer-lavf-o","protocol_whitelist=file")):
            if mpv.mpv_set_option_string(
                    handle,key.encode(),value.encode())<0:return False
        if mpv.mpv_initialize(handle)<0:return False
        api=c.create_string_buffer(b"sw")
        create=(Param*2)(Param(1,c.cast(api,c.c_void_p)),Param(0,None))
        if mpv.mpv_render_context_create(
                c.byref(ctx),handle,create)<0:return False
        notifications=[0]
        def signal(_):
            # Exactly the same C callback policy as the Switch player:
            # signal only; do NOT call into libmpv from its own callback.
            notifications[0]+=1
        cb=callback_type(signal)
        mpv.mpv_render_context_set_update_callback(ctx,cb,None)
        cmd=(c.c_char_p*4)(b"loadfile",str(path.resolve()).encode(),
                           b"replace",None)
        if mpv.mpv_command(handle,cmd)<0:return False

        width,height=320,180
        size=(c.c_int*2)(width,height)
        fmt=c.create_string_buffer(b"rgb0")
        stride=c.c_size_t(width*4)
        pixels=c.create_string_buffer(width*height*4)
        params=(Param*5)(
            Param(17,c.cast(size,c.c_void_p)),
            Param(18,c.cast(fmt,c.c_void_p)),
            Param(19,c.cast(c.pointer(stride),c.c_void_p)),
            Param(20,c.cast(pixels,c.c_void_p)),
            Param(0,None))
        frames=0
        nonblack=False
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            flags=mpv.mpv_render_context_update(ctx)
            if flags & 1:
                if mpv.mpv_render_context_render(ctx,params)<0:return False
                frames+=1
                raw=pixels.raw
                # Real testsrc2 frames must contain actual nonzero RGB.
                if any(raw[i] for i in range(0,len(raw),4096)):
                    nonblack=True
                mpv.mpv_render_context_report_swap(ctx)
                if frames>=3 and nonblack and notifications[0]>0:
                    return True
            time.sleep(.016)
        return False
    finally:
        if ctx.value:
            mpv.mpv_render_context_set_update_callback(
                ctx,callback_type(),None)
            mpv.mpv_render_context_free(ctx)
        mpv.mpv_terminate_destroy(handle)


if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit(2)
    raise SystemExit(0 if verify(pathlib.Path(sys.argv[1])) else 1)
