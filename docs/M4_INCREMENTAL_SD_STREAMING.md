# M4 — Incremental MPEG-TS validation and disposable SD segment writer

**Engineering checkpoint, October 12, 2026 — experimental M4 only.**

This tranche implements the **file-writing half** of the low-RAM
streaming design requested for TizenTube NX. It is not yet evidence that
the complete network-to-decoder route meets its RAM target.

## Implementation

`include/tizentube_nx/playback/streaming_ts_validator.hpp`

- Validates 188-byte transport packet sync independently of TLS/network
  chunk boundaries (even if a packet arrives one byte at a time).
- Rejects incomplete segments, overlong payloads, unexpected content,
  missing H.264 or AAC PAT/PMT stream evidence and invalid final alignment.
- Keeps at most **65,536 bytes of verification data**, independent of
  segment length. Limits one segment to 16 MiB maximum.
- Uses the project's existing reviewed `inspect_mpeg_ts` parser;
  stream-type evidence is NOT a substitute for actual decoder acceptance.

`RollingHlsSink::append_streamed()`

- Uses an app-owned `O_EXCL` temporary `.part` media file. A producer
  delivers **at most 32 KiB per chunk**, e.g. from the native approved
  CDN HTTPS receive loop (upcoming transport step).
- Checks the rolling ~24 MiB media quota *during each chunk*, before
  the disk write. SD bytes must never grow with viewing duration.
- On complete validated media: `fsync`, close, rename to private
  segment filename, then publish the local HLS playlist.
- On invalid data, cancelled producer, partial write, fsync failure,
  missing audio/video or abnormal completion: do not publish that media,
  unlink temporary files and preserve/reconcile SD quota. A failed
  unlink poisons the sink instead of understating occupied storage.
- Reuses the established playlist/backup/rollback mechanism.
- The experimental `RollingMuxedHlsSession::pump()` now exercises
  this sink entry point by dividing its existing verified HTTP body
  into bounded 32-KiB pieces. This is a **working sink integration**,
  not just a new header. It is guarded by the existing M4 opt-in flag.

## What remains incomplete (critical)

The current `HlsSegmentReader::fetch()` delegates to
`net::HttpClient::perform()`, which materializes the HTTP body and
can retain up to 16 MiB **before** invoking the SD chunk writer.
Therefore a sub-64-KiB **validator allocation** does NOT mean sub-64-KiB
**total video memory**. The next transport step must replace this
fetch with incremental Horizon SSL read-to-file callbacks and verified
HTTP response headers. Network cancellation and redirect/host policy
must remain identical or stricter. No new media URL bypass.

M4 still cannot delete segments while the pinned libmpv decoder has
an active lease because per-segment FD-release evidence is missing.
Thus long, high-bitrate videos will eventually stop downloading at
the 24 MiB bound. The 720p/source-native FPS, independent AAC,
24 MiB cache, total app <=100,000,000 decimal SD bytes and
zero-persistent-watched-media requirements are **acceptance gates**,
not already completed features.

The hardware-accepted M3 player and its full-download implementation
are unchanged. No new default or accepted hardware NRO is enabled by
this change.

## Executed tests

Locally executed against the exact submitted
`streaming_ts_validator.hpp` with the repository's actual
`src/core/ts_streams.cpp` parsing algorithm:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 \
  -Iinclude tests/streaming_ts_validator_tests.cpp \
  src/core/ts_streams.cpp -o /tmp/ttnx-streaming-ts
/tmp/ttnx-streaming-ts
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude tests/streaming_ts_validator_tests.cpp \
  src/core/ts_streams.cpp -o /tmp/ttnx-streaming-ts-san
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/ttnx-streaming-ts-san
```

Both passed: one-byte/randomly split packets, 7/187/188/191/32000-byte
producer steps, 940-KiB fake TS, 64-KiB buffer bound, oversize,
corrupt midstream sync, missing AAC, incomplete packet, disallowed
repeated finish/push.

The real `RollingHlsSink` test suite now also covers streamed
7-byte chunk publication, broken TS rollback, cancelled producer,
oversized 32-KiB+1 chunks and zero-cache teardown. **Those updated
sink tests and integrated native builds are committed but NOT YET
EXECUTED/VERIFIED** as of this checkpoint: they depend on currently
queued GitHub host/devkitA64 workflows, and this local workspace lacks
the full repo checkout/native toolchain.

CI setup updates link the actual TS parser into sink tests and the
FFmpeg growing-playlist writer, and register standalone validator
normal/sanitizer tests. Do not mark full end-to-end streaming accepted
until the exact native and physical Switch gates pass.
