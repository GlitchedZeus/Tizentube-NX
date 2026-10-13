# TizenTube NX — definitive RAM streaming and installed-size contract

**Accepted product requirement (2026-10-12): video and audio are streamed, not saved.**

This document supersedes the SD-backed rolling HLS design and the old
"100 MB = NRO + 24 MiB of SD media + 8 MiB settings" accounting in
`M4_STORAGE_BUDGET.md`. That document remains as historical engineering
evidence, NOT the current architecture or product acceptance criteria.

## Media path — zero SD media writes

1. The native approved YouTube/CDN transport fetches and validates media
   incrementally. Audio/video bytes pass through **bounded volatile RAM**
   to libmpv/FFmpeg for immediate playback. Streaming requires receiving
   data; "not downloaded" means **no saved media file or persistent cache**.
2. No normal playback path creates `.mp4`, `.ts`, `.m4a`, HLS segment,
   local HLS media file, downloaded thumbnail media, or media playlist on
   the SD card. No named media files are staged on SD even temporarily.
3. The player starts as soon as a **small verified playable prefix** is
   available, not after the full clip has arrived. Network reads pause on
   RAM backpressure; already-consumed in-memory data is reclaimed safely.
4. Stop, B/back, seek cancellation, video switch, error, sleep/wake and
   shutdown invalidate/cancel outstanding fetches and release volatile
   media buffers. No replayable offline media survives a process exit.
5. Long videos must work without RAM or SD use growing with duration.
   Seeking can request a new range/window and reinitialize the decoder;
   it must not require saving video files.
6. No libmpv URL access to external sites. The approved native transport
   retains its URL and privacy checks; a callback-based read-only stream
   (for example `mpv_stream_cb_add_ro`) can hand volatile bytes directly
   to the decoder, subject to actual pinned Switch libmpv ABI verification.
   Confirm blocking read, cancellation, end-of-stream and teardown behavior.
7. Both muxed H264/AAC and separately provided A/V are product targets.
   Do not silently sacrifice audio, seeking or frame-rate fidelity.
   Source selection remains at most 720p, at the source's real fps.

## Installed-size cap (SD only)

The **entire finished installation and persistent app-owned state** must
fit within **100,000,000 decimal bytes**: NRO, bundled art/resources,
settings, minimal bounded logs, and any other persistent app files.
RAM-held media bytes are **NOT** part of installed size. No 24-MiB SD
media reservation exists in the accepted architecture. The NRO+assets
can use the budget left after reserved bounded settings/logs.
The 100 MB number is an upper bound, **not a target size**.

Volatile playback RAM must have a separate, explicitly measured,
bounded budget; implementers should begin with a small few-second ring,
then tune for stable 720p decoding on Switch. Never make RAM unbounded
because it is exempt from the installed-size cap.

## Staging policy

The existing hardware-tested M3 player with visible video and AAC audio
is preserved as a regression baseline **only**. Its full-media SD-cache
transport is an interim development implementation, **not acceptable
for final release**.

Previously authored M4 `RollingHlsSink`, `RollingMuxedHlsSession`,
SD-backed worker, HLS playlist probe and related cleanup tests are
**superseded experiments**. Do not integrate their SD writer into the
user-facing M4 player, present it as streaming compliance, or continue
optimizing a 24 MiB disk ring as the release architecture.

## Required engineering and acceptance gates

- Implement a bounded RAM producer/consumer transport with cancellation,
  backpressure, timeouts, seek epoch invalidation and buffer reclamation.
- Prove via host tests and sanitizers that a long video does not grow
  memory with duration and that stop/seek leaves zero live media buffers.
- Verify the actual Switch libmpv custom-stream integration and preserve
  the known-good frame renderer, audible AAC, controller behavior and
  sleep/wake.
- Instrument/deny app-owned media writes while playing; run one long
  video and repeated plays on hardware, and verify no SD media artifacts.
- Measure installed footprint before/after playback, seek, cancel and
  app restart; keep it at or below 100,000,000 bytes with bounded logs.
- Run normal, ASan/UBSan, native toolchain and hardware gates before
  calling the RAM streaming route accepted or releasing a new NRO.
- Never merge the draft PR or displace the hardware-good M3 path merely
  because a host proof compiled.

All other project rules still apply: no Shorts, no ads, no tracking-heavy
links, canonical watch sharing, and no surprise cached media.
