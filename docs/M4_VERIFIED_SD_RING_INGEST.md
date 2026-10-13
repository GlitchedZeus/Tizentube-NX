# M4 upstream-first HTTPS → verified SD ring → libmpv callback tranche

**Status: Experimental implementation, October 13, 2026.**
This is an actual C++ source integration milestone building on the
Switch-NewPipe-inspired `ttnxring://media` mpv stream callbacks.
The hardware-approved M3 player/launcher is NOT modified.

## Engineering changes

1. `RollingFileStream` now offers transactional writes in the same
   app-owned **bounded** SD file:
   - `begin_segment(Content-Length)` reserves a declared media segment
     within the remaining storage window, capped at 16 MiB.
   - `append_staged()` accepts ≤32-KiB network pieces. These bytes exist
     on disk but are deliberately **not readable** by the decoder.
   - `commit_segment()` verifies exact written length, fsyncs, then
     atomically exposes the completed byte range and wakes blocked reads.
   - `abort_segment()` rolls back the unpublished byte cursor after
     cancelled network traffic, invalid TS or malformed completion.
   - Existing non-transactional `append()` is mutually exclusive with a
     staged transaction. No active reader can force a segment retirement.
   - No duplicate 16-MiB staging file and no video-sized RAM buffer.
2. `VerifiedRingHlsIngest::ingest()` consumes the **real signature** of
   `HlsSegmentReader::stream(url, admit, chunk)`:
   - Header-time declared-size admission precedes every payload chunk.
   - `StreamingTsValidator` checks every 188-byte TS sync boundary and
     bounded PAT/PMT H.264 + AAC stream evidence (≤64-KiB retained
     validation sample).
   - No segments become visible to libmpv until HTTP completed with
     exactly the announced size, TS media checks passed, and fsync
     succeeded. Failure/overrun/cancellation leaves the previous
     playable byte window intact.
   - Backpressure is returned explicitly when the fixed ring is full,
     rather than creating a second cache file.
3. `switch/source/m4_mpv_stream_cb_probe.cpp` now exposes a real,
   **opt-in** typed C++ integration entry point that accepts
   `HlsSegmentReader`, validates the signed CDN resource and feeds its
   stream directly to the same bounded ring that libmpv opens through
   the `ttnxring://media` read/seek/cancel/close callbacks.
   This still requires the caller to own the ring and callback bridge
   through the whole decoder lifetime.

## Host regression execution

Using actual local source with C++20 `-Wall -Wextra -Wpedantic -Werror`:

- Existing 100-MiB ring test through 64-KiB physical storage:
  **normal and ASan/UBSan passed** after the staged-write change.
- Existing mpv stream-callback bridge:
  **normal and ASan/UBSan passed**.
- New `verified_ring_hls_ingest_tests.cpp`:
  **normal and ASan/UBSan passed**. It verifies 7-byte and 188-byte
  network chunks, in-order TS publication, no active-reader eviction,
  bounded wraparound reuse, pre-body capacity refusal, partial
  transport loss, corrupted sync, length mismatch and invalid URL
  never publishing media.
- New `rolling_ring_atomic_commit_tests.cpp`:
  **normal and ASan/UBSan passed**. A real concurrent decoder
  reader blocks while network data is uncommitted and wakes after
  commit; abort and cancellation leave no exposed partial media.
- All four suites are registered in host CTest and the normal/sanitizer
  GitHub workflow. No unrun suite is claimed as green.

## Exact acceptance limitations

The experimental native probe uses the correct libmpv callback ABI,
but the **full devkitA64 exact-head compile and physical Switch** are
still required. This does NOT yet mean the default TizenTube NX
player streams YouTube videos through the new ring.

- A live HLS producer still needs integration with a source quality
  choice and session owner/worker lifetime, and media must remain
  approved CDN HTTPS only. The typed ingress source function exists;
  the user-facing M3 playback route still uses its accepted cache.
- While a callback reader is open, the ring **refuses eviction**.
  Continuous videos exceeding ~25 MB therefore still need a verified
  decoder-safe lifetime strategy; no unsafe timestamp-based unlinking.
- Only self-contained muxed H.264/AAC MPEG-TS is accepted; independent
  video/audio, fMP4 and arbitrary UMP are not implied to work.
- Total real SD bytes and libmpv's decode buffers must be measured
  on the target Switch, with the user's sysmodules running.
- Verify no residual watched video after stop, restart or crash:
  normal ring-owner teardown deletes its file, while orphan cleanup
  after abnormal termination is a separate required gate.
- No Google accounts or profiles, no Shorts, no saved videos, and
  combined app+settings+Library+media remains limited to
  100,000,000 decimal SD bytes.

## Reuse evidence

The mpv callback architecture, including NULL size_fn and callback
cookies, was derived from the **source-level design** in
mirusu400/switch-newpipe
`1cd590e212807a200ca5d90043051ce0f8cdbfb3`,
GPL-3.0. The transactional bounded ring/validator is original
TizenTube NX code to meet stricter SD and low-RAM requirements.
No upstream copyrighted assets or proprietary YouTube NSP binary
were imported.
