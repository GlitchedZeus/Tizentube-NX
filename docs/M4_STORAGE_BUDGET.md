# M4 historical SD-backed storage experiments — superseded

> **Product requirement update (2026-10-12):** The planned M4 SD-backed rolling HLS media cache is **not the final architecture**. TizenTube NX must stream verified media through bounded **RAM only**, with **zero media files on SD**. The **100,000,000-byte limit applies to the full installed app and persistent app-owned files**, not to video buffers or a video-download cache. Read [the definitive RAM streaming contract](M4_RAM_STREAMING_REQUIREMENTS.md) before extending M4. The rest of this document describes obsolete or isolated historical experiments, not release acceptance criteria.

Product target: the TizenTube NX NRO (including embedded assets), all app-owned
settings/diagnostics, and the **maximum simultaneous disposable video/audio
buffer** must consume no more than **100,000,000 bytes** of SD storage.

Proposed auditable budget:
- NRO + embedded assets: at most 66,445,568 bytes (CI size gate).
- Rolling video/audio working set: at most 25,165,824 bytes (24 MiB).
- App-owned settings, essential logs and metadata: at most 8,388,608 bytes
  (8 MiB; future runtime enforcement required).

The sum of these three reservations is exactly 100,000,000 bytes, not 100 MiB.
No saved videos, thumbnails library, watch history, analytics telemetry,
advertising IDs, or unbounded diagnostics may live outside this budget.

**Not yet achieved:** M3 currently downloads full temporary video/audio
before regular playback; its existing native HLS and MP4 cache path can still
use substantially more than 100 MB. The pure RollingMediaBudget scheduler
added here proves accounting and safe eviction rules under normal and
sanitizer host tests, but is not wired to mpv, the native cache or the
network transport. No new NRO should be labeled 100-MB compliant until the
streaming pipeline, persistent-state quota and cleanup/seek gates all pass
on actual hardware. The accepted M3 renderer is deliberately untouched.

Required next steps:
1. Build a verified 720p-or-lower streaming source with early H.264/AAC
   readiness. Target a *few seconds* of decoded-ready media; do not promise
   zero startup delay for slow networks or unavailable media.
2. Stage a local-only playlist/transport that libmpv can consume while
   producer fetches further verified media. Keep signed CDN URL handling
   inside the existing approved native transport. Do not grant libmpv
   direct Internet protocols.
3. Pin active decoder segments, unlink behind a two-second protection
   window only after the file is closed, then commit reclaimed quota.
   Back-pressure producers when the 24 MiB reservation is full.
4. On seeks outside the retained window, cancel and join producers, release
   decoder leases, purge old segments, then reopen a verified seek window.
   Fail safely on CDN expiry, sleep/wake, network drops or SD write failures.
5. Validate stable A/V sync, 720p scaling, native available frame rate,
   speed, no temporary-media growth over long videos, and cache=0 at exit.
6. Enforce an actual 8 MiB limit on app-owned settings and diagnostic logs
   and verify the end-to-end 100 MB cap on hardware.

Privacy and interface intent: no Shorts, ads or promoted content, no
app-added tracking or analytics, no persistent watched-video cache, and
canonical share URLs. Network requests to YouTube/CDN are still necessary
to stream video; do not misrepresent this as invisible or anonymous to
the upstream provider. Preserve user-initiated settings only as needed.

**Release gate:** all host normal/ASan/UBSan checks, native Switch build,
NRO size check and on-device full-view 720p video/pause/seek/cleanup test.
No automatic merge of PR #8 and no regression to accepted real playback.

## M4 native rolling stream coordinator (staged, not Switch-accepted)

The separate header-only RollingStreamCoordinator now reserves bandwidth
and on-SD bytes *before* each approved media fetch, counts in-flight media
against the same 24 MiB cap, and publishes verified media accounting only
after an exact-sized durable segment is reported. Unverified media,
stale-generation callbacks, forged/tampered ticket fields and failed storage
transactions cannot advance the prepared startup state. Decoder-pinned
segments never release quota; a caller acknowledges actual safe file deletion
before the coordinator frees capacity. Backpressure protects the budget when
the player pauses. Seeking creates a new timeline anchor (no mistaken
three-second readiness at t=120 seconds) and refuses to forget old media
until callers have joined downloads, released decoder leases and purged bytes.

A simulation runs 1,200 three-second video/AAC pairs (one hour) with a
small rolling window, plus 720p/60 source selection, stale callbacks,
failed writes, pin/unpin, quota exhaustion and arbitrary-time seeks.
Both normal -Wall -Wextra -Werror and ASan/UBSan tests ran locally.
The workflow also runs these deterministic sanitizer tests in CI.

IMPORTANT: **not yet connected to actual libnx HTTP, HLS event demux or
the native libmpv playback pipeline.** It is a bounded producer/consumer
admission layer, NOT a proven fast-start video player. The previously
accepted visible M3 video remains the only hardware-accepted candidate.

## File-backed M4 rolling HLS backend (opt-in, not hardware-proven)

The new `RollingHlsSink` writes verified-by-caller, muxed MPEG-TS data
into an app-owned rolling directory. It uses exclusive file creation,
fsync, bounded segment names and relative-only local HLS media playlists.
The `EXT-X-MEDIA-SEQUENCE` advances as decoded segments retire; the
playlist excludes consumed segments before their SD files are deleted.

Host regression testing covers 1,200 actual filesystem segment writes and
window evictions (one hour of three-second segments), a zero-storage final
discard, oversized writes, segment gaps, symlinked paths, unknown files
and active decoder leases. Both normal and ASan/UBSan builds passed locally.
The native sink is disabled unless explicitly compiled for M4, leaving
the currently hardware-proven M3 player unchanged.

Outstanding acceptance gates: integrate the verified H264/AAC HTTP
producer, decoder-owned segment pinning, and mpv local HLS live-manifest
reload; validate on Nintendo Switch that replacing the playlist file
works with SD/FAT; prove a 24-MiB rolling limit and no persistent media
during 720p playback. Full M3 caching may still exceed 100 MB; this
new backend is not claimed production-active.

## M4 reviewed-network to rolling-HLS integration (opt-in)

`RollingMuxedHlsSession` connects the existing policy-constrained
`HlsSegmentReader` to the actual bounded SD segment sink and the
`RollingStreamCoordinator`. It has a worker-driven, one-fetch-per-pump
API, a 720p/60fps selection ceiling, pre-request 16-MiB worst-case
reservations, CDN URL validation, full TS packet sync checking, H.264/AAC
PAT/PMT evidence, and a three-second metadata-prepared playback prefix.
No signed URL enters a local playlist.

The current initial transport accepts **only** muxed H.264/AAC MPEG-TS
VOD, so independent AAC playlists and fragmented MP4 continue using the
already accepted M3 complete-cache fallback. Consumed segments are
deletable only after decoder-release confirmation and the two-second
playback guard. The producer is backpressured without new HTTPS calls
when the disk reservation is low, and the final teardown requires joined
workers, a closed player and successful SD cleanup.

The host test runs the real HLS HTTP reader through an offline fake
HTTP transport, codec-negatives, 403 handling, unsafe-source rejection,
high-bitrate backpressure, restart-after-consumption, completed HLS
playlists, and clean exit. Build, CTest and an ASan/UBSan target are
registered in the host workflow.

This is a real transport-to-files implementation but **NOT YET connected
to the Switch libmpv demuxer or released as the default player**.
The full 100 MB and fast-start product acceptance gates remain blocked.

## Verified progressive **real** H264/AAC host decode (M4 acceptance evidence)

The `rolling_hls_live_decode_tests.py` suite now generates a fully local
12-second H264/AAC video from FFmpeg's synthetic video and sine generators,
splits it into six 2-second TS segments, and drives the **actual**
`RollingHlsSink` via a compiled C++ writer as the video becomes available.
A separate FFmpeg process opens the incomplete local playlist after the
first two segments. With the network protocol whitelist restricted to
`file,crypto,data`, it incrementally decodes both tracks, reloads the
growing manifest and reaches at least 11 seconds before teardown.
The measured host proof reached approximately 12.03 seconds without errors.

The companion cancellation test interrupts the writer before ENDLIST,
confirms that incomplete media is *not* advertised as a finished VOD, and
waits for explicit decoder-close confirmation before reclaiming all media.
Both tests passed in normal and ASan/UBSan-compiled writer modes locally.
These tests are registered as separate required host CI steps following
the existing FFmpeg installation.

A second correction prevents metadata exhaustion in long videos:
`RollingStreamCoordinator` now treats its 192-segment ledger limit as
pre-fetch **backpressure**, not a fatal failure after writing the next
segment. An independent normal and sanitizer regression exercises filling
the ledger, refusing the 193rd network request, retiring one played item,
and resuming downloads without losing the session.

**Current limits:** this is a real host FFmpeg A/V decode proof, NOT
validated Switch libmpv behavior. Actual 720p/60 hardware throughput,
separate HLS AAC streams, player-driven segment-release events,
resume/seek/sleep on device, and overall 100-MB SD accounting remain
required before enabling this route in the real NRO.

## Real decoder-driven rolling eviction regression

A third offline full-A/V test now starts FFmpeg against a growing local HLS
playlist and watches decoder progress. After at least five seconds have been
decoded, it asks the writer to retire segment zero, verifies that the actual
TS file is unlinked, checks the playlist's moving
`EXT-X-MEDIA-SEQUENCE:1`, and requires the decoder to reach at least
11 seconds without losing either track. The writer does **not** discard the
remaining media until the test signals that the decoder has closed.
Normal and ASan/UBSan local execution passed all **three** live-decoder
tests, including early cancellation and zero-cache teardown.

This is host proof of HLS reload/eviction under FFmpeg. It does not prove
libmpv's FDs are released at the same point on Switch; per-segment decoder
lifetime and FAT rename behavior must pass on real hardware before live
eviction is enabled for users.

## M4 experimental local libmpv opening (compiled but not user-accessible)

The hardware-proven M3 `CachedMpv` renderer now has a **compile-gated**
`TTNX_M4_PLAYER_PROBE=1` mode (requires both `TTNX_M3_INTEGRATED=1`
and `TTNX_M4_ROLLING_STREAM=1`). It reuses the proven Borealis
visibility workaround, mpv software render callback, NanoVG frame upload,
AAC selection gate and exclusive decoder lease rather than introducing
a replacement drawing pipeline.

In this mode alone, the local-file demuxer protocol whitelist is
`file,crypto,data` (still no HTTP/HTTPS); `loadfile` receives only the
fixed app-owned rolling `stream.m3u8` path. An independent bounded,
host-tested `LocalLiveHlsGate` parses the exact sink-produced playlist
format and rejects remote URLs, path traversal, encryption, extra tags,
bad segment numbers, excessive metadata and an insufficient A/V prefix.
Seeking/replaying evicted segments is disabled pending a safe producer
re-prime/decoder lifecycle. Normal M3 playback options and UI routes
remain unchanged. The new function has **no caller from main.cpp**.

The integrated GitHub devkitA64 workflow now builds a second, **non-uploaded**
probe NRO with all M4 flags to catch native compilation/link errors, while
still packaging **only** the original M3 integrated candidate. Host tests
include the local gate under normal and ASan/UBSan, independent inspection
of the exact on-SD playlist produced by `RollingHlsSink`, and static
isolation checks for the renderer and absence of an ordinary UI route.

**Remaining acceptance blockers:** even when it compiles, the native probe
is not approved for distribution or hardware playback because the app's
network producer and the player activity do not yet have coordinated,
worker-joined teardown or measured native libmpv live-HLS behavior.
No main-screen playback action has been changed. Do not label the new M4
mode working or say that 100 MB total SD usage is achieved.

## FAT-compatible rolling HLS playlist replacement and rollback

The original file-backed M4 sink used rename-over-existing when updating
`stream.m3u8`. libnx SD filesystems need not provide POSIX replacement
semantics. The experimental backend now publishes via a fixed, app-owned
`stream.m3u8.previous` backup, promotes a fully flushed temporary file,
and restores the prior manifest when promotion fails. Failed segment
transactions still remove unpublished TS bytes before releasing quota;
cleanup whitelists the backup while refusing symlinks or unfamiliar files.

Host tests inject promotion failure after moving the original playlist,
prove byte-for-byte manifest restoration, prove no new TS bytes remain,
then retry successfully and discard the whole session. The normal and
ASan/UBSan host sink tests passed, and the three FFmpeg-generated real
H264/AAC progressive host tests still passed with the revised publisher.

This publication is **not fully atomic to a reader**: a brief pathname
gap exists between moving the old playlist and promoting the new one.
Before enabling M4 on a Switch, validate libmpv's playlist reload retry
behavior on actual SD/FAT/exFAT under frequent refresh, pause/sleep and
power interruption. Keep the hardware-proven complete-cache M3 fallback
until that gate passes.

## Opt-in M4 native worker lifecycle — threaded producer ownership

The experimental `RollingHlsWorker` now owns one real `std::thread` for
a `RollingMuxedHlsSession`, using the `RollingMuxedHlsWorkerJob` bridge.
The producer keeps all approved HTTPS requests, codec inspection, segment
fsync and playlist publication **off** the Borealis/render thread.
The UI side receives only a locked, bounded state snapshot
(Fetching/PrefixReady/Waiting/Complete/Failed/StopRequested/Joined/Cleared).
There are no URLs, cookies, video bytes or HTTP client pointers in
the published status.

At backpressure the worker **waits on a condition_variable**, not a
busy polling loop. Only a bounded one-slot decoder-confirmed retirement
command can release previous segments. Requests to retire before decoder
release, or duplicate pending commands, are rejected. The worker's
stop request does not unlink anything; explicit `join_downloads()` must
finish all in-flight network activity and `cleanup_after_decoder_closed()`
refuses to delete files if the native libmpv lease remains active.

The tests run successful prefix, backpressure stall, no repeated HTTP
polling, rejected early retirement, successful release/resume, exception
failure handling, stop and join ordering, player-lease-protected cleanup,
and reuse for consecutive videos. Normal -Wall/-Wextra/-Werror and
ASan/UBSan passed locally; a ten-run deterministic repetition also
passed. These checks are registered as `rolling_hls_worker` in CTest,
a separate sanitizer job and a native opt-in devkitA64 object check.

**Explicit remaining limitation:** this is the worker/transport side of
the native integration, not permission to enable M4 in the existing UI.
No trustworthy libmpv per-segment file-descriptor release signal has yet
been connected. Therefore native UI must not guess a release from video
time alone, and joining a potentially timed-out HTTPS request must never
happen inside the draw callback. If the player still owns media or
downloaders have not joined, zero-cache cleanup must fail closed.
The M3 accepted picture/audio path remains the default.

## M4 native producer/decoder lifecycle ownership and FD-race closure

The standalone M4 worker previously accepted a caller's
`decoder_released=true` flag without checking whether an HLS decoder was
active. This was unsafe for the current pinned Switch libmpv: no dependable
per-segment file-descriptor-release signal has been demonstrated.

The worker now blocks segment retirement during any active global
`HlsPlaybackLease`. Its new `enter_player_mode()` handshake also
**atomically closes the retirement queue** before the owner acquires the
decoder lease and refuses to open a decoder while an unlink callback is
already executing. An early request cannot race media deletion against
mpv initialization. This handoff also accepts a verified video that finished
downloading before the UI had a chance to open it.

`RollingPlaybackOwner` owns the worker `Job`, so the network callback can
never outlive its feed. Its state machine requires a verified prefix,
exclusive decoder ownership, a worker stop and join, and explicit
post-`mpv_terminate_destroy` notification before clearing any local media.
If the cleanup callback fails, ownership and SD accounting remain available
for retry. A job that fails during HTTPS download can still be joined and
cleaned safely without ever opening a player. `RollingHlsRuntime` ties the
actual `HlsSegmentReader`, `RollingHlsSink`, `RollingMuxedHlsSession`,
the native worker adapter and this owner into one lifetime.

The new host owner tests passed `-Wall -Wextra -Werror` and ASan+UBSan,
along with 70 repeated worker/owner normal executions. Additional
worker tests prove that an in-flight unlink prevents the player handoff,
subsequent retirements are refused, and quickly completed playlists still
open. Existing rolling sink, media budget, stream coordinator and local HLS
manifest tests also passed locally. Full native runtime integration is
registered in CMake host testing and separate opt-in Switch object checks.

**Do not enable in the regular app yet:** the existing `CachedMpv` also
acquires its own exclusive `HlsPlaybackLease`; M4 must be taught to use
the single lease owned by `RollingHlsRuntime`, not acquire a second one.
More importantly, because per-segment decoder FD release remains unproven,
no segments can be evicted while the decoder runs. An extended high-bitrate
video will therefore hit the 24-MiB rolling limit and pause, rather than
violating the SD cap. That is an explicit acceptance blocker; do not ship
a bounded long-form player until this is solved and hardware-tested.
No currently accepted M3 playback or main-menu code was modified.
