# Upstream-first native streaming: Switch-NewPipe callback adaptation (M4)

**Engineering date: 2026-10-13.** Experimental PR #8.
**Accepted physical M3 10-minute video/audio checkpoint is not changed.**

## Why this implementation exists

The Switch-NewPipe codebase already solved an important platform problem:
native **libmpv stream callbacks**, not a full browser/runtime, can bridge
a network producer and a decoder on Nintendo Switch.

Inspected exact upstream source:
- https://github.com/mirusu400/switch-newpipe/blob/1cd590e212807a200ca5d90043051ce0f8cdbfb3/src/switch/switch_player.cpp
- mpv 0.36 ABI definition (actual pinned Switch libmpv family):
  https://github.com/mpv-player/mpv/blob/v0.36.0/libmpv/stream_cb.h
- Upstream design details:
  https://github.com/mirusu400/switch-newpipe/blob/main/docs/playback.md
  and docs/potoken-720p-findings.md

Their switchcache:// stream open/read/seek/close callbacks and
**deliberately NULL size_fn** are the reusable design choices.
Supplying the declared total fragmented-MP4 file size can prompt
FFmpeg's mov_read_mfra() to seek to an end-of-file index that has
not arrived in the cache. This is confirmed by their code and notes,
not inferred from the official proprietary Switch YouTube NSP.

**Where we deliberately diverge:** Switch-NewPipe's cache accumulates
downloaded bytes and does not meet the user's fixed ~25 MB disposable
SD cache. This feature therefore has our own bounded SD ring and
strict reader-lifetime state. The existing imported UMP framing
provenance is separately covered by docs/THIRD_PARTY_MEDIA.md.

## New, executable C++ modules

- include/tizentube_nx/playback/rolling_file_stream.hpp:
  fixed-size **24 MiB minus 128 KiB** maximum app-owned file,
  32-KiB append cap, two-piece wrap writes via pwrite, absolute
  read cursor via pread, bounded physical file high-water accounting,
  exact retained-byte window, seek refusal outside the downloaded
  range, condition-variable wake on network arrival or cancellation.
  **No 25 MB RAM video buffer.**
- include/tizentube_nx/playback/rolling_mpv_stream_bridge.hpp:
  actual mpv_stream_cb_add_ro()/stream_cb.h callback signatures,
  ttnxring://media ONLY, read/seek/cancel/close and NULL size_fn.
  Each callback cookie independently retains a shared reference to
  the backing file until mpv invokes close_fn. Signed Googlevideo
  URLs can never become libmpv stream destinations in this bridge.
- switch/source/m4_mpv_stream_cb_probe.cpp/.hpp:
  a separate native linker/ABI integration function compiled only
  with TTNX_M3_INTEGRATED=1, TTNX_M4_ROLLING_STREAM=1 and
  TTNX_M4_RING_STREAM_CB_PROBE=1. Ownership is returned to the caller
  and MUST survive through mpv_terminate_destroy. No main-menu
  activation, default player change or unverified release artifact.

## Eviction and shutdown rules

- **Never retire** retained media while an mpv callback Reader cookie
  is still alive. A playhead timestamp or apparent frame completion
  is not proof that libmpv closed all stream-reader handles.
- ring.release_before(absolute) only succeeds after every decoder
  cookie closes, and only for an absolute value in the current
  retained byte range.
- **Never** use an unbounded live appending file: append refuses
  backpressure when the 24MiB-minus-headroom SD allowance is full.
- Buffered seeks work only within [retained_start, written_end].
  No forward seek that would wait for an unbounded upstream download.
- On cancellation, pending reads wake and stop; destructing the final
  stream owner closes the FD and unlinks the app-owned video file.
- The app startup must still prune any **app-owned** file orphaned by
  a crash/power loss. Do not delete unknown user directories/files.

## Verified host execution

The exact GitHub blob contents were locally compiled with g++ 14 using
C++20, -Wall -Wextra -Wpedantic -Werror, -pthread, both normal
-O2 and ASan/UBSan -O1 -g -fno-omit-frame-pointer. **All four runs
passed.**

Tests cover 64KiB wrap read/write correctness; no reader-time
eviction; buffered-only seeking; 32KiB append backpressure;
100MiB of synthetic data transmitted and verified with a **64KiB
physical SD file**, under explicit reader-close/retirement cycles;
zero temporary media after teardown; wake/cancel of a waiting read;
symlink refusal without touching target files; correct
mpv_stream_cb_add_ro wiring; invalid URL refusal; callback
size_fn deliberately NULL; cancellation through a callback cookie.

The 100MiB test is synthetic byte-accurate transport, **not a
claim of 100MiB real YouTube playback**, codec support, or
uninterrupted long video through one live libmpv decoder lease.
No native Switch compilation, ROMFS/player integration or hardware
NRO result was verified locally (no devkitA64 in container).

CTest/host-sanitizer workflow now runs both suites, and the integrated
native workflow additionally compiles/links a separately named
m4_stream_cb_abi candidate NRO against real pinned mpv headers.
It does **not** upload/offer that untested candidate to the user.

## Remaining work before release

1. Connect the native HTTPS producer to this buffered callback transport
   while preserving strict CDN policy, typed H.264/AAC validation and
   audio synchronization. A custom mpv URL itself must stay local-only.
2. **Choose and implement a tested decoder lifetime strategy.** While
   any read callback is open the ring deliberately refuses overwrite;
   it will correctly backpressure at ~25MB instead of corrupting video.
   Long playback must use a hardware-proven retirement method or a
   decoder teardown/reprime with safe continuity.
3. Support separate audio/video streams within ONE combined storage
   budget, not two independent 24MiB files. Muxed TS is not sufficient
   for every YouTube video.
4. Measure real RAM including decoded frames with sysmodules loaded,
   startup time, seek, pause/sleep/wake, network cancellation and
   crash cleanup. The cap refers to installed app + persistent Library +
   transient SD media collectively <=100,000,000 decimal bytes.
5. Run full exact-head CTest/ASan/UBSan, devkitA64 NRO build, then
   physical Switch picture/audio/10+minute playback acceptance.
6. Never enable the new path by default or replace the accepted M3
   artifact before these gates succeed.

**Source/license:** Switch-NewPipe is GPL-3.0 licensed; TizenTube NX is
GPL-3.0-or-later. This uses documented callback architecture with
new bounded-ring implementation and explicit attribution. Preserve
the upstream citation and any copyright notices on actual copied
fragments. No Google/YT proprietary NSP code/assets are imported.
