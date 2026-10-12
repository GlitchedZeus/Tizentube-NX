# M4 storage and latency target: ≤100,000,000 bytes total

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
