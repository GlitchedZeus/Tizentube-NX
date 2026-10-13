# M4 playback-aware prefetch + native decoder feedback (2026-10-13)

## Engineering goal and user constraints

The previous owned M4 producer called the approved HTTPS transport as fast
as it could until a roughly 25 MB ring reached backpressure. This could
download much more than the user had watched, especially while paused or
when the libmpv renderer had not yet delivered a frame.

This tranche adds **time-bounded fetch admission** to the existing
Switch-NewPipe-inspired M4 stream bridge, not a second media system.

## Actual source changes

1. `RollingPrefetchWindow` validates a strict VOD manifest timeline:
   - 1–4,096 segments; each duration 1–60,000 ms
   - combined duration at most 12 hours; integer-overflow safe
   - fetch only when the next segment starts no more than **15,000 ms**
     ahead of the decoder's *verified* playback clock
   - time is relative to the first segment in the current restart epoch
   - rejected/invalid plans cannot partially modify an approved window.
2. The owned `RollingStreamEpoch` now accepts the exact validated
   segment-duration list from `create_native_ring_epoch`. A condition
   variable pauses the single producer thread when the next segment
   lies outside the playback window. It sends **no additional CDN GET**
   while waiting. `advance_playback_ms` wakes the worker, but only
   after a decoder has been registered. Regressive, out-of-duration or
   stopped-session position changes are refused.
3. The experimental native `CachedMpv` presentation sends libmpv
   `time-pos` back to the producer only after it has rendered real
   video frames AND selected an audio track. Thus a bogus UI clock,
   empty audio stream or failed preparation cannot unlock downloads.
4. X Stop / Exit on a native ring candidate immediately requests
   producer cancellation. The owner then joins the network worker,
   destroys the decoder and retires only app-owned temporary media.
   The cancel wake uses a local shared_ptr so concurrent cleanup
   cannot invalidate the ring before its reader is awakened.
5. H.264/AAC ring playback now uses the same selected-audio gate as
   the accepted completed HLS player. A missing AAC track is not
   silently treated as a successful normal-video playback.
6. Numeric ring diagnostics append a small bounded storage count,
   segment count and waiting status. No signed media URL, HTTP header,
   session token or video file content is written to diagnostics.

**Flag isolation:** All M4-specific libmpv code remains guarded by
`TTNX_M4_RING_STREAM_CB_PROBE`. The normal M3 renderer, video files,
Search route and hardware-good checkpoint remain unchanged. The user
cannot launch this experimental candidate from production menus.

## Verified normal and sanitizer evidence

Locally executed against the exact new
`rolling_prefetch_window.hpp` and
`tests/rolling_prefetch_window_tests.cpp` source bytes:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
  tests/rolling_prefetch_window_tests.cpp -o /tmp/ttnx_prefetch_normal
/tmp/ttnx_prefetch_normal
g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  tests/rolling_prefetch_window_tests.cpp -o /tmp/ttnx_prefetch_san
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/ttnx_prefetch_san
```
Both **passed**. The tests cover 3s segment timing (first six chunks,
then pause), correct advancement at 2,999/3,000ms, restart at segment 6,
out-of-range indices, invalid durations, huge playback positions, and
transactional reconfiguration.

`rolling_stream_epoch_prefetch_tests.cpp` exercises the actual
`RollingStreamEpoch`, its worker thread and mpv callback stub:
12 validated 3-second TS segments; only six prefetched at t=0,
no progress before decoder attachment, one additional segment at
3 seconds, complete transfer after true decoder progress, safe
decoder-close cleanup, new segment-8 epoch and immediate cancellation
from a waiting phase. Registered in host CTest and normal/ASan/UBSan CI.

**The full owned-epoch integration test, native compilation and actual
Switch hardware have NOT yet run successfully in this environment**:
GitHub Actions is currently queued/pending, and the container has no
devkitA64 or full checked-out repository. Do not call the integrated
player compiled, verified on hardware or production-ready.

## Remaining hard blockers

- This limits speculative downloading; it does **not** establish
  seamless eviction while libmpv has an open stream callback. Once
  25MB is truly occupied, safe decoder-close/re-prime is still
  necessary. Hardware must establish acceptable A/V continuity.
- libmpv may probe ahead while video frame delivery is stalled. The
  15-second lead needs real Switch tuning and might need a safer
  minimum verified prefix or decoder buffering control.
- Quality/fps/source resolution must match what the video's actual
  chosen YouTube stream supplies. Separate AAC/video and fMP4 remain
  blocked; muxed MPEG-TS does not cover every video.
- Measure native H.264/AAC audio, 720p60 when source-supported,
  sleep/wake, seeking, startup time, physical RAM with sysmodules
  loaded and total SD <=100,000,000 decimal bytes.
- Delete all watched media on normal stop and fixed-name orphans
  at startup, including after crash; no Google account, Shorts,
  or permanent video cache.
