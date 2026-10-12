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

## Field-confirmed October 12 redirect failure

The October 12 hardware log proves a full 7,849,910-byte AAC transfer
(HTTP 206), followed by H.264 download failure at 11,534,336 of
21,356,101 bytes when subsequent Range requests repeatedly returned
HTTP 302. Another source attempt encountered HTTP 302 before receiving
any AAC bytes. Neither attempt reached the libmpv decoder.

Previously the native media transport surfaced all 302 responses as
terminal failures, and the range reader retried them as generic socket
errors. The updated media reader may follow **at most two** HTTP
301/302/303/307/308 hops per bounded Range request, only to approved
HTTPS Googlevideo /videoplayback destinations. The original exact Range
and identity Accept headers remain unchanged. Every hop repeats the
native pre-DNS media-host gate and TLS certificate/hostname checks.
No arbitrary redirects, Nintendo hosts, off-CDN destinations, unsigned
media URL logs, MIME relaxations, or full HTTP 200 fallbacks are added.
The terminal media response must still be an exact validated HTTP 206
with the same resource size across chunks. Redirect loops and unreviewed
locations fail closed. Tests cover first, middle and final chunk 302,
unsafe destinations, loops, missing/duplicate Location and hop limits.

TRANSFER REDIRECT <phase> <hop-count> 0 now records the number of
reviewed hops *only*; no signed URL is serialized. The triage script
distinguishes legacy CDN_REDIRECT_UNHANDLED from newly unsafe Location
rejections and redirect limits.

This removes one demonstrated transport blocker but is not a physical
video/audio acceptance. The first full Switch decode and framebuffer
presentation remain pending hardware evidence.

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

## October 12 first real Switch decoded-picture milestone and hidden-player fix

The physical run of the CDN redirect repair verified *actual native playback
progress*, not just a host media fixture. Two complete separate A/V cache
transactions published video + audio. The player reported a selected H.264
video track and a selected AAC audio track, decoded geometry 854x480, more
than 350 successful software frames in its first session, and non-black RGB
pixel samples in its frame diagnostics. Audio was audible. However, no
`UI DRAW` occurred during those frames. Immediately after Stop/Back, the
log recorded its first `UI READY`, `UI DRAW`, and `UI PRESENTED`, while a
screenshot captured the briefly uncovered video image and RGB test bars.

The cause was traced against the *pinned* Borealis submodule at commit
`20e2d33b6c4ffce139ce304c503c04f5b94da920`: `Application::pushActivity`
zeros incoming opaque activity opacity while fading out the previous
activity; `View::show()` then returns early without restoring opacity if
its root was never marked hidden. The M3 activity previously returned a
newly constructed, unhidden Box. Its decoder continued running behind a
fully transparent activity. On B/Stop the hide animation resets opacity to
1 before fading out, temporarily exposing the decoded picture.

The activity now explicitly calls `root->hide([]{},false,0.0f)` *before*
returning the content to `pushActivity`, so Borealis performs the real
fade-in at the correct point in its lifecycle. This is a targeted M3
fix, not a modification to the accepted M2 flow or Borealis submodule.

A non-destructive watchdog logs `UI FAIL 2 <frame-count> <root-alpha*1000>`
if 90 rendered frames occur with zero video-widget draws; numeric-only,
no signed URLs or frame contents. The video texture is now released when
the player view is destroyed, avoiding repeated video-session GPU leaks.
Host regression tests model the pinned fade behavior and require the
specific pre-push visibility initialization and watchdog. A new physical
run is still mandatory to accept uninterrupted picture, audio, replay,
pause/resume, seeking and Back behavior.

## Ephemeral playback cache policy (accepted M3 checkpoint preserved)

TizenTube NX is **not a video downloader or persistent offline library**.
Its SD media-cache holds only the current temporary playable file pair (or
HLS segments) for rewind, pause and Replay while the player is open. After
libmpv closes all file descriptors and releases its exclusive lease, the
M3 player destructor removes completed audio, video, backup/partial paths,
and HLS ready/pending/previous files. A failed/cancelled download is swept
as soon as its worker joins, and a power-loss residue is swept on next app
startup *before* another new media session begins. If cleanup fails, a new
video request refuses to proceed rather than silently accumulating more
cache. Never delete settings, search data or numeric SD diagnostics.

The storage deletion API is a narrowly scoped whitelist, uses lstat() to
reject symlinks or other unexpected types, and rejects cleanup when a
playback lease is active. The HLS cleanup only enumerates its three known
media directories and only deletes the known playlist/audio/segment names.
Unknown files are preserved and cause a fail-closed result. This deletion
is ordinary filesystem unlinking, not forensic secure-erasure of SD flash.
A future streaming/ring-buffer player can remove the need for complete
pre-downloads; that work must be hardware-tested independently.

## M3 HLS external-AAC SD startup optimization

A pending HLS external-AAC download now buffers its bounded chunks in one
private file descriptor instead of reopening and fsyncing for every 1 MiB.
At publish, fflush/fsync/close must succeed **before** length and MP4
structure verification and the atomic ready/ swap. Abort closes the file
before deleting known staging entries. This reduces unnecessary SD syncs
without exposing partial AAC to libmpv; the accepted playback engine and
HTTPS source policy remain unchanged.

## Quit-app cleanup: worker join before SD disposal

Quitting from Search while a transfer is still running does not call the
regular UI completion handler. M3 shutdown now requests cancellation,
joins the media worker (and its joined parallel AAC/H.264 child), then
tries to purge app-owned temporary media. If libmpv is still actively
playing, its exclusive lease prevents unsafe removal; the startup sweep
on the next launch remains the fallback. Numeric-only CACHE PASS/FAIL phase
7 indicates whether immediate exit cleanup succeeded.

## Temporary storage ceilings and global cleanup preflight

HLS now has aggregate **per-clip budgets** of 512 MiB video and 128 MiB
audio (including external AAC), rather than only a 16 MiB per-segment
limit. The adaptive MP4 paths retain their 512 MiB video / 128 MiB audio
limits. HLS manifest and bookkeeping bytes add a small overhead. These
are bounds per transaction, **not an absolute peak SD reservation**:
a prior published HLS clip and its replacement can coexist briefly
during a transaction. True bounded-buffer streaming has not been
implemented; full media downloads still precede normal playback.

MP4 cache cleanup inventories all known fixed paths before deleting any;
HLS inventories pending/previous/ready in full before deleting from any
of them. Unexpected file types and symlinked SD-directory ancestors
cause a fail-closed result, preserving unrelated files. These are
defense-in-depth checks, not a sandbox against concurrent hostile
filesystem mutations. Native regression cases exercise budget
exhaustion, poisoned neighboring directories, symlinks, active leases
and repeat-view cleanup.
