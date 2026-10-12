# TizenTube NX M3 — Hardware Playback Diagnostic Capture

This dedicated experimental M3 build records only known event categories
and numerical measurements in the fixed SD card file:

`sdmc:/switch/TizenTube-NX/m3-playback.log`

The previous rotated file is `m3-playback.previous.log`. The active
file is bounded to approximately 192 KiB; rotation preserves a previous
192 KiB window. One playback request starts a recording session; each
append is flushed and fsynced to survive the next crash or quit.

A successful download is **not proof** of video decoding or on-screen
presentation. Capture these independent checkpoints:

- SOURCE/HLS/DIRECT: source acceptance, error enum codes, available media
  format counts, and chosen H.264/AAC route.
- TRANSFER/CACHE: anonymous HTTPS status, transport enum, byte progress,
  separately signed AAC/video completion, SD-cache verification.
- PLAYER/DECODE: libmpv initialization, loadfile events, selected video
  and audio tracks, codec categories, time position, audio gate.
- RENDER/UI: decoder output properties, software render-context updates,
  successful RGB frame rendering, non-black sampled pixels, NanoVG
  texture creation/update, actual view draw calls, and visibility of a
  permanent red/green/blue display-control marker.

No signed googlevideo URLs, visitor data, response payloads, cookies,
account information, local media bytes, title, or search query are
recorded. The logger's API accepts **only enumerated stage/event values
and signed integers**, and fails closed when SD logging is unavailable.
It never logs mpv's arbitrary text log messages.

## One-round physical repro

1. Install this exact experimental integrated NRO separately from M2.
2. Open Search and play a normal video where you previously heard AAC but
   saw a black screen. Let it play for at least 15 seconds.
3. Press pause/resume, seek once, replay if necessary. Note whether the
   permanent R/G/B calibration strip is visible in the player screen.
4. Quit TizenTube NX normally; copy `m3-playback.log` and (if present)
   `m3-playback.previous.log` from your SD card into this chat.
5. If you cannot access SD immediately, take a photograph of the player
   status/codec/frames display and say whether audio and RGB bars appeared.

Use the logs to classify the *first* failure in the chain; do not issue
another random playback fix for the last visible symptom. Separate host
libmpv decoding proves a local software capability, but real Switch A/V
and UI rendering still require this hardware evidence.

Accepted M2 stays unchanged; keep PR #8 unmerged.

## Offline automatic triage

After copying the SD log to Linux, run

`python3 tools/m3_diagnostics_triage.py /path/to/m3-playback.log`

The tool reads ONLY fixed typed numeric events and emits one coarse fault
category, such as `AUDIO_ONLY_TRACKS`,
`VIDEO_RENDER_NOT_PRODUCING_FRAMES`, `BLACK_RENDERED_FRAMES`,
`UI_VIEW_NOT_DRAWN`, `CHECK_TEXTURE_AND_DISPLAY`, or
`AAC_DOWNLOAD_FAILED`. It does not access any network service. A
classification is evidence for the next focused fix, not proof of the
ultimate decoder bug or DRM compatibility.
