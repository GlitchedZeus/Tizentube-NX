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

**libmpv warning privacy:** The diagnostic player requests `warn`-level
mpv events, but discards all arbitrary message text and source prefixes
immediately. Only coarse fixed category numbers (video, render, demux,
audio, ffmpeg, other) and the integer severity are saved, capped to 64
warnings per session. This helps distinguish missing decoders from a
NanoVG presentation failure without ever persisting signed URLs.

The native video view logs `UI READY` on its NanoVG texture creation,
`UI PRESENTED` each successful texture-upload attempt, and `UI DRAW`
independently of decoded content. A video can have audio, a selected
H.264 track and non-black software framebuffer pixels while the GPU
still presents nothing; the separate checkpoint makes that distinguishable.

### AAC completion failure diagnosis and recovery

The hardware log proved all three signed HTTP 206 AAC ranges reached their
exact declared sizes, but `MediaDownloadError::SinkFailure` stopped the
transfer at `finish()`. Previously a single catch-all code hid
`fflush`, `fsync`, `fclose`, `lstat`, backup and final rename errors.
The cache sink now retains **typed failure-stage and errno numbers**,
and promotes completed files using a temporary fixed-path backup instead
of renaming over an existing SD file, which Horizon may not allow.
If promotion fails, it restores the prior complete file and never opens
a truncated one. A new download can recover a prior file after an
interrupted promotion. It still requires successful `fflush` and
`fsync`, and keeps the active libmpv lease protection.

### Exact post-download SD failure codes

New `CACHE FAIL <phase> <stage> <errno>` entries accompany successful
HTTP 206 responses followed by storage failures. Phase 4 denotes AAC;
5 is H.264 video, 3 is a muxed MP4. Storage stages are 1 open, 2 write,
3 flush, 4 sync, 5 close, 6 size, 7 backup, 8 promotion, 9 rollback.
All values are numeric, with no URLs, titles or media bytes. The offline
triage script now reports e.g. `AAC_SD_PROMOTE_FAILED` instead of blaming
the CDN when all promised range bytes arrived.

## Render-loop fix for the original black-video report

The real libmpv render API requires timely update callbacks for
correct video output. The integrated M3 player previously never
registered one, and called `mpv_render_context_update()` only from
the Borealis video widget's `draw()` hook. Audio could progress while
video updates were starved, and `new_frame_` was cleared before the
widget could upload a frame if drawing was delayed. The repaired player
registers an atomic-only video update callback, consumes the queued
video frame from the app's **main UI tick**, preserves the last RGBA
frame until NanoVG actually uploads it, and unregisters its callback
before freeing the mpv context. Periodic typed `RENDER STATUS` entries
now record the frame flag, count of update notifications and frame
count separately. This does not assert physical Switch playback without
a new hardware run.

### Exact host frame-render proof

Host CI now generates a real local 45-second H.264 video-only MP4,
opens it through libmpv's **software render-context API**, registers
the same notify-only update callback used by the Switch and requires
at least three **non-black RGB decoded frames**. Selecting audio or
opening a playable local file is no longer sufficient to pass this
render test. The separate production contract asserts that the Switch
UI tick drives the renderer and NanoVG acknowledges textures only
after actual upload. This does not replace physical Switch evidence.

### Video decode interoperability safeguard

The integrated player's output is the libmpv **software** renderer
(`MPV_RENDER_API_TYPE_SW`), whose frames must be CPU-readable RGB
memory before NanoVG can upload them. The isolated M3 code now explicitly
uses `hwdec=no` for this path, rather than offering `hwdec=auto`
hardware surfaces that might not be exported to software output. This
prioritizes image correctness over peak hardware decode efficiency until
a proper pinned Switch GPU-backed renderer has been validated.
The already accepted M2 browsing build and earlier offline checkpoint
are unchanged.
