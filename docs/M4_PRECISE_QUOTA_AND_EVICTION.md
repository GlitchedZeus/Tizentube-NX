# M4 — precise stream admission, decoder-safe retirement, and long-video window

**Engineering checkpoint:** October 12, 2026. **PR #8 experimental M4 only.**
The accepted M3 10-minute visible-video/AAC hardware checkpoint remains
untouched. No default NRO, network allowlist, Google profile or Shorts
behavior changes.

## The actual cache-admission defect

Earlier M4 code reserved the maximum **16 MiB** for *every* unknown-size
segment, then refused another fetch once less than 16 MiB remained. With a
rolling media capacity of **25,034,752 bytes** (24 MiB minus 128 KiB of
metadata headroom), this could stall video at only about **8 MiB** of
stored segments, long before the small 25 MB SD-cache budget was used.

**Now:**
1. The Horizon SSL / HTTP/1.1 media parser validates 200 status, media
   Content-Type, one exact Content-Length, no redirect/compression or
   ambiguous Transfer-Encoding.
2. It invokes a new bounded-size admission callback **before delivering
   even one byte of the media body**.
3. `RollingMuxedHlsSession` first consults
   `PhysicalCacheAdmission::decide(occupied,declared)` against the exact
   **25,034,752-byte** physical SD file allowance. It then reserves the
   **declared length**, not an imaginary 16-MiB worst case, in
   `RollingStreamCoordinator`. No SD `.part` body bytes are emitted
   until *both* admission gates succeed.
4. The sink's unchanged streaming TS validator still checks 188-byte
   packet boundaries and H.264/AAC PSI, fsyncs the exact data and publishes
   only after a complete network body and a matching commit ticket.
5. A native adapter cannot silently fall back to the legacy 16-MiB
   whole-response reader when `TTNX_M4_ROLLING_STREAM` is enabled.
6. Cheap metadata/window preflight avoids opening another CDN connection
   when the 30-second ahead window or the 192-entry ledger is already full.

**Backpressure stays reversible**, not a hard decoder failure: if a
genuine Content-Length exceeds remaining SD capacity, the transport
refuses it before a body write, keeps already validated files, and
waits for a *safe*, externally verified decoder-close retirement.

## Decoder lifetime and eviction audit

A separate defect let `RollingMuxedHlsSession::retire_before()` advance
the coordinator's playhead before checking whether all requested segments
were actually old enough for deletion. It now checks the full guard
window and global `HlsPlaybackLease` **before** mutating the coordinator
playhead. The lower-level `RollingHlsSink::retire_before()` also refuses
an active lease, in case a caller bypasses the worker.

If a segment unlink fails after the new manifest has published, the sink
now disables subsequent appends instead of silently continuing with
inconsistent physical bytes versus ledger state. Normal shutdown still
attempts the strict app-owned directory preflight and cleanup.

The host end-to-end M4 regression now exercises **14 x 1,128,000-byte
H.264/AAC TS segments** (3-second timestamps). The initial 11 segments
consume **12,408,000 bytes**, exceeding the old ~8 MiB stall threshold
while remaining within the physical cache limit. Fetching next waits at
the intended 30-second ahead boundary **without a CDN request**.
An active decoder lease vetoes deletion. After the lease is released,
old segments can be retired under the 2-second age guard and later
segments fetched again. The fixture finally closes the session and
asserts **zero temporary SD media**.

**Important boundary:** this test simulates a confirmed closed decoder.
It does not prove that pinned Switch libmpv releases individual HLS
files during *active* playback. Never infer safe eviction from timestamp
alone. A long real video may still stop downloading at the cache
limit until the decoder lifetime issue is solved by hardware-proven
FD release or a controlled stop/join/reprime transition.

## Tests actually executed in local host container

These tests used the exact GitHub blob contents for the pure HTTP
and physical quota components:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 \
  -Iinclude tests/http1_media_stream_tests.cpp -o /tmp/http1
/tmp/http1
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude tests/http1_media_stream_tests.cpp -o /tmp/http1-san
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/http1-san
# Repeat the corresponding commands for physical_cache_admission_tests.cpp.
```

Passed: HTTP normal, HTTP ASan/UBSan, physical quota normal, physical
quota ASan/UBSan, and **30 repeated normal runs** across both suites.
The fragmented HTTP test now proves verified Content-Length is
announced *once and before the first emitted body chunk*, including a
refused-cache callback that delivers zero media bytes.

The modified `rolling_muxed_hls_tests.cpp`,
`rolling_stream_coordinator_tests.cpp`, and
`rolling_hls_sink_tests.cpp` are **committed and added to existing host
CI** but have **not executed locally against the full repository** in
this session. Do not report those regression suites, full host CTest,
native devkitA64 compilation or Switch hardware as passed until the
exact-head workflows actually run.

## Remaining release gates

- Compile and run complete updated M4 host, sanitizer and native lanes.
- Attach the experimental streaming worker to a *single* owned libmpv
  lease without modifying accepted M3 playback.
- Prove continuous high-bitrate playback with bounded, safely evicted
  SD media, no decoder FD races and correct independent AAC.
- Measure RAM with the user's Switch sysmodules loaded; the small
  HTTP/TS buffers are **not** the total libmpv decode memory.
- Verify startup, seek, pause, sleep/wake, early abort and repeated
  playback on physical hardware; clean leftover app-owned media
  after crashes or app restarts.
- Confirm **all app-owned SD bytes** combined (NRO, assets, tiny
  settings/Library/logs and live media cache) are <=100,000,000 bytes,
  with no videos left after cleanup.
