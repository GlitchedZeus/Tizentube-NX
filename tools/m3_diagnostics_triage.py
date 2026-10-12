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
    for row in transfer:
        phase,code,bytes_read=row[2:]
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
    if not frames and (of("DECODE","LOADED") or of("DECODE","RESTART")):
        return "VIDEO_RENDER_NOT_PRODUCING_FRAMES","Media loaded, but no successful software-rendered video frames were observed."
    if frames and all(x[3]==0 for x in frames):
        return "BLACK_RENDERED_FRAMES","libmpv reports render success, but all sampled RGB values are black."
    if frames and any(x[3]>0 for x in frames) and not ui_draws:
        return "UI_VIEW_NOT_DRAWN","Non-black frames were rendered but the Borealis video view did not report drawing."
    if frames and any(x[3]>0 for x in frames) and ui_draws:
        return "CHECK_TEXTURE_AND_DISPLAY","Non-black frames and UI draws both exist; check texture upload and whether RGB calibration bars were visible."
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
