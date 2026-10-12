#!/usr/bin/env python3
"""Offline TizenTube NX M3 field-log triage (no network or credentials)."""
from __future__ import annotations
import argparse
import pathlib
import re

LINE=re.compile(
    r"^(\d{1,12}) (SESSION|SOURCE|HLS|DIRECT|TRANSFER|CACHE|PLAYER|DECODE|RENDER|UI) "
    r"([A-Z_]{2,24}) (-?\d{1,19}) (-?\d{1,19}) (-?\d{1,19})$")
MAX_LOG=205_000

def parse(text: str):
    rows=[]
    for line in text.splitlines():
        match=LINE.fullmatch(line)
        if match:
            rows.append((match.group(2),match.group(3),*(
                int(match.group(i)) for i in (4,5,6))))
    last=next((i for i in range(len(rows)-1,-1,-1)
               if rows[i][0:2]==("SESSION","START")),0)
    return rows[last:]

def assess(text: str):
    rows=parse(text)
    if not rows:
        return "NO_FIELD_EVENTS", "No valid M3 diagnostic records found."
    of=lambda st,ev:[x for x in rows if x[0]==st and x[1]==ev]
    transfer=of("TRANSFER","END")
    failed=of("TRANSFER","FAIL")
    if failed:
        return "DOWNLOAD_START_FAILURE","Safe CDN range or SD staging rejected before transfer."
    cache_errors=[x for x in of("CACHE","FAIL") if x[2] in (3,4,5)]
    for row in cache_errors:
        phase,stage,os_error=row[2:]
        causes={1:"OPEN",2:"WRITE",3:"FLUSH",4:"SYNC",5:"CLOSE",
                6:"FILE_SIZE",7:"BACKUP",8:"PROMOTE",9:"RESTORE"}
        if stage in causes:
            return ("AAC_SD_" if phase==4 else "VIDEO_SD_")+causes[stage]+"_FAILED", (
                f"SD finalization failed in {causes[stage]} (numeric errno {os_error}); "
                "no network retry can repair this local failure.")
    # Location and signed source URLs are never logged. Distinguish a
    # legacy 302 treated as a transport retry from a new verified policy
    # rejection or the hard two-hop redirect limit.
    statuses=of("TRANSFER","STATUS")
    retries=of("TRANSFER","RETRY")
    for row in transfer:
        phase,code,bytes_read=row[2:]
        if code>=1000 and code%1000 in (301,302,303,307,308):
            media_status=next((s for s in reversed(statuses) if s[2]==phase),None)
            failure=media_status[3] if media_status else None
            if failure==5:
                return "UNSAFE_MEDIA_REDIRECT",(
                    f"Phase {phase} rejected an unreviewed or cyclic CDN Location; "
                    "no unsafe destination was contacted.")
            if failure==11:
                return "MEDIA_REDIRECT_LIMIT",(
                    f"Phase {phase} exceeded two approved CDN redirects per range.")
            if any(r[2]==phase and r[3]==302 for r in retries):
                return "CDN_REDIRECT_UNHANDLED",(
                    f"Phase {phase} received HTTP 302 after {bytes_read} bytes; "
                    "the old native range downloader rejected that relocation.")
        # a=phase (3 muxed, 4 AAC, 5 video),
        # b=download-error*1000 + HTTP code; c=received bytes.
        if code>=1000:
            return ("AAC_DOWNLOAD_FAILED" if phase==4 else
                    "VIDEO_DOWNLOAD_FAILED"), f"Phase {phase} ended with failure class {code//1000}, HTTP {code%1000}, received {bytes_read} bytes."
    errors=of("PLAYER","ERROR")+of("RENDER","ERROR")+of("RENDER","FAIL")
    if errors:
        return "PLAYER_RENDER_API_ERROR","mpv initialization, software render, or native GPU texture API returned an error."
    end_errors=[x for x in of("DECODE","END") if x[3]<0]
    if end_errors:
        return "MEDIA_DEMUX_DECODER_ERROR","libmpv stopped with a negative native error code."
    tracks=of("DECODE","SELECTED")
    if tracks and tracks[-1][2]==0 and tracks[-1][3]==1:
        return "AUDIO_ONLY_TRACKS","AAC is selected but video track is not selected by libmpv."
    stats=of("DECODE","STATUS")
    snapshots=[x for x in stats if x[2]!=1 and x[2]!=2]
    if snapshots and snapshots[-1][4]==0:
        return "NO_SELECTED_VIDEO","Playback is active but no video track is selected."
    frames=of("RENDER","FRAME")
    ui_draws=of("UI","DRAW")
    # Presentation activity fade bug: a 0-alpha root prevents ALL widget
    # draws until Borealis hide() temporarily forces alpha back to 1.
    # An upload after Stop is not proof that picture was visible in playback.
    no_draw_warnings=[x for x in of("UI","FAIL") if x[2]==2]
    if no_draw_warnings:
        last=no_draw_warnings[-1]
        return "PLAYER_ACTIVITY_NOT_VISIBLE",(
            f"{last[3]} software frames decoded with zero video-widget draws; "
            f"activity alpha x1000={last[4]}. Review Borealis show/hide lifecycle.")
    if frames and any(x[3]>0 for x in frames) and ui_draws:
        first_stop=next((i for i,x in enumerate(rows)
                         if x[:2]==("PLAYER","STOP")),None)
        if first_stop is not None:
            drawn_before_stop=any(x[:2]==("UI","DRAW")
                                  for x in rows[:first_stop])
            rendered_before_stop=any(x[:2]==("RENDER","FRAME") and x[3]>0
                                     for x in rows[:first_stop])
            if rendered_before_stop and not drawn_before_stop:
                return "PLAYER_ACTIVITY_NOT_VISIBLE",(
                    "Non-black software frames were rendered before Stop, "
                    "but the first player view draw occurred only during exit; "
                    "inspect incoming activity opacity and Borealis transitions.")
    if not frames and (of("DECODE","LOADED") or of("DECODE","RESTART")):
        return "VIDEO_RENDER_NOT_PRODUCING_FRAMES","Media loaded, but no successful software-rendered video frames were observed."
    if frames and all(x[3]==0 for x in frames):
        return "BLACK_RENDERED_FRAMES","libmpv reports render success, but all sampled RGB values are black."
    if frames and any(x[3]>0 for x in frames) and not ui_draws:
        return "UI_VIEW_NOT_DRAWN","Non-black frames were rendered but the Borealis video view did not report drawing."
    if frames and any(x[3]>0 for x in frames) and ui_draws:
        uploads=of("UI","PRESENTED")
        if not uploads:
            return "TEXTURE_UPLOAD_NOT_OBSERVED","Non-black frames exist but no NanoVG texture upload was reported."
        return "CHECK_TEXTURE_AND_DISPLAY","Non-black frames, UI draw calls and texture uploads exist; check whether RGB calibration bars were visible."
    if of("HLS","STATUS") and not of("CACHE","PASS") and not of("TRANSFER","END"):
        return "SOURCE_NOT_READY","No completed and verified playable media cache was observed."
    return "INCOMPLETE_OR_UNRESOLVED","The log does not establish the first fault yet; collect a full 15-second run."

def main():
    p=argparse.ArgumentParser()
    p.add_argument("log",type=pathlib.Path)
    args=p.parse_args()
    if args.log.stat().st_size>MAX_LOG:
        p.error("Active log exceeds fixed capture budget")
    code,description=assess(args.log.read_text(errors="replace"))
    print(f"CLASS={code}")
    print(description)
    print("Only typed numeric events were parsed. No URLs or raw log payloads were displayed.")

if __name__=="__main__":
    main()
