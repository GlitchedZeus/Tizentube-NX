# M4 real Horizon SSL → disposable SD streaming tranche

## Scope and production boundary

The user requires streamed media with **no complete video downloads**, a
roughly **25 MB disposable SD buffer**, minimal extra RAM, and
**100,000,000 decimal bytes total on SD** (NRO + resources + history +
settings/logs + active media). No Google accounts, Shorts or saved video
files. The accepted hardware-tested M3 player must remain unchanged.

This major engineering tranche adds a **new experimental native streaming
transport**, rather than modifying the accepted M3 whole-response HTTP
helper.

## Actual C++ implementation

1. `include/tizentube_nx/net/http1_media_stream.hpp` implements a
   bounded HTTP/1.1 media-specific streaming parser:
   - Accepts only status 200 and one strict Content-Length (376 to
     16,777,216 bytes); validates MPEG-TS-oriented Content-Type.
   - Rejects 3xx and error bodies, Content-Range, Location, transfer
     encoding/chunked framing, missing or duplicate Content-Length,
     compression except identity, malformed headers and bytes past EOF.
   - Handles arbitrary header/body split boundaries, supports callback
     refusal and rejects short EOF. No video response-body allocation.
   - Caps SSL read calls at **32 KiB**, HTTP header length at 16 KiB,
     and temporary cross-boundary parsing data at about 48 KiB.
2. `LibnxHlsSegmentTransport::stream_segment()` is a dedicated
   **media-only** Horizon SSL socket path:
   - Runs original signed-googlevideo URL checks before DNS and within
     `tcp_connect_allowed_host`; HTTPS 443, full host/date/CA TLS
     verification, no libmpv networking and no redirect following.
   - Reuses the reviewed libnx TLS setup/cleanup pattern; receives into
     a stack-allocated 32-KiB buffer, feeds the parser, and emits body
     chunks straight to the caller. No entire HTTP response string.
   - A transfer is successful only after the exact declared byte count
     has reached the consumer.
3. `HlsSegmentReader::stream()` selects an injected
   `HlsSegmentStreamTransport`. The old fetch adapter remains only for
   **host test fakes**; on Switch with `TTNX_M4_ROLLING_STREAM` it
   **fails closed if no native streaming adapter is provided**.
4. `RollingMuxedHlsSession::pump()` now uses `reader_.stream()`
   and `RollingHlsSink::append_streamed()`, passing verified chunks
   through the existing 188-byte TS boundary/PAT/PMT checks to an
   unpublished, private `.part` file. The native worker still enforces
   its admission/quota/codec checks and returns no playable manifest
   until fsync and verified publication succeed.
5. `RollingHlsRuntime` accepts the explicit native streaming
   transport pointer; host fake runtimes can still use their existing
   compatibility reader.

**Important**: The native TLS setup code is duplicated only into the
separate M4 experimental function for now; the existing accepted
`perform_single_request` and M3 player are deliberately untouched.

## Executed tests

The committed `http1_media_stream.hpp` and
`tests/http1_media_stream_tests.cpp` exactly match locally compiled
sources. Executed actual `g++` C++20 `-Wall -Wextra -Wpedantic -Werror`
normal `-O2` and sanitized `-O1 -g
-fsanitize=address,undefined -fno-omit-frame-pointer`, both **passed**.

Host coverage includes:
- byte-by-byte through 32-KiB TLS receive splits;
- real delimiter crossed between reads with a full data-containing chunk;
- exact Content-Length completion and byte-for-byte emission;
- redirects, double/missing/bogus lengths, compressed/chunked traffic,
  unsupported MIME, Content-Range, short EOF, overrun, consumer
  cancellation, and oversized individual recv chunks.

Updated `rolling_muxed_hls_tests.cpp` additionally injects a fake
native stream that emits >1-MiB segments in 32-KiB pieces, asserts
**zero** fallback `HttpClient::perform()` calls, verifies publication
and bounded SD use, then simulates a mid-body failure and insists on
full cleanup. **Those modified integration tests have not yet executed
locally or in exact-head CI**, so do not call them passed.

Host CTest/sanitizer CI now registers the parser. Experimental
devkitA64 CI explicitly checks the compiled native media client and
HLS segment worker objects. The exact-head workflow must pass before
any new NRO can be called build-verified.

## Remaining critical blockers

- M4 is still **opt-in and not hooked into the accepted M3 activity**.
  The M4 runtime's native stream adapter must be instantiated by the
  future hardware-test player owner and linked to its decoder lease.
- Native devkitA64 build and physical Switch are not available in the
  local container. The new libnx SSL method is **not yet compiler- or
  hardware-verified**; check CI and fix any compile failures first.
- Segment retirement still requires positive per-segment decoder FD
  release evidence; do not guess based on playhead time alone. A
  high-bitrate long video can legitimately pause at the 24-MiB quota
  until eviction is proven safe.
- Independent audio/video HLS and fMP4 still need an approved muxing
  strategy. Muxed TS alone does not establish general YouTube 720p
  compatibility.
- Transport/validator memory bounds are **not** an overall physical
  RAM measurement. libmpv/FFmpeg decoding, GPU resources, OS and
  sysmodules require separate device measurements, and installation
  must meet the total 100 MB cap.
- Native stream worker must honor stop/join before decoder destruction,
  clear cache on normal exit and recover app-owned orphaned temp files
  at next startup; physical sleep/wake, seek, pause and restart testing
  remains mandatory.

No Nintendo/YouTube proprietary binary, Google account credentials,
video file, signed URL or decrypted official NSP resource was committed.

## Accepted-M3 binary isolation hardening

A follow-up commit places the new SSL-to-SD native method, interface
inheritance and stream parser include behind **TTNX_M4_ROLLING_STREAM**,
in addition to the existing media-plane guard. As a result, an M3
integrated build without the M4 flag **does not compile or link** the
new SSL reader or its stream-vtable entry; its accepted HTTP behavior
remains on the original code path. This is a source-level isolation
control, pending native CI confirmation.
