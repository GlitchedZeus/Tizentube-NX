# M4 accepted streaming architecture — low-RAM, bounded SD working cache

**Accepted product requirement, corrected October 12, 2026.**
This supersedes the RAM-only plan in `M4_RAM_STREAMING_REQUIREMENTS.md`.
The user prioritizes available memory for Switch sysmodules; playback must
not reserve a large RAM video/audio buffer. A small **disposable SD working
cache of approximately 25 MB** is explicitly allowed and preferred.

## Playback behavior

- Stream video and audio incrementally: begin decoding after a small,
  verified playable prefix arrives, **without fetching the whole video**.
- Use a **rolling, file-backed SD cache**, shared by video and audio, with a
  hard quota. Consume old segments and reclaim storage during playback;
  never retain watched clips, save entire videos for replay, or create a
  permanent/offline media library.
- Keep only minimal network, decoder and frame-memory buffers needed for
  operation. Those inevitable allocations must be bounded and measured
  under actual Switch homebrew/sysmodule memory pressure.
- Never allocate a 25 MB **RAM** media ring. In particular, replace the
  current `HlsSegmentReader::fetch()` whole-response/whole-segment RAM
  materialization before calling low-memory streaming complete. Instead,
  implement bounded chunked network-to-SD transport and incremental codec
  validation as permitted by the approved HTTPS policy.
- Constrain source video to up to 720p at its **actual source frame rate**,
  including 60 fps when the source provides it. Preserve audible AAC and
  correct A/V synchronization. Account for memory used by libmpv and the
  existing 640x360 software-render surface; do not claim zero RAM usage.
- An SD cache is a temporary stream working set, **not a downloaded video**.
  On stop, B/back, video switch, cancellation and app exit, join the network
  producer, completely close libmpv, and remove temporary media. On the next
  launch, safely clean any app-owned stale media left after a crash or sleep
  interruption. Never remove unknown or user-owned files.
- Seeking beyond the retained SD window should re-fetch just the required
  window. Never keep the whole video to make seeking possible.

## Total SD budget: 100,000,000 decimal bytes maximum

All app-owned SD bytes count simultaneously, even while playback is running.

| Category | Maximum |
| --- | ---: |
| Installed NRO + bundled assets | 66,445,568 bytes |
| Temporary rolling media cache, including its metadata allowance | 25,165,824 bytes (24 MiB, approximately 25 MB) |
| Settings, minimal diagnostics and other persistent app files | 8,388,608 bytes (8 MiB) |
| **Total hard ceiling** | **100,000,000 bytes** |

These are *upper limits*, not intended allocation sizes. Media files should
consume **zero bytes after complete teardown**; settings may remain.
CI must measure NRO+embedded assets, and native runtime tests must bound
every app-owned file. Logging, failed download parts and manifests count.
Preserving room for user sysmodules in RAM is a separate resource constraint.

## Critical engineering gates

1. Keep the known-good M3 picture/audio player as a hardware-regression
   baseline. Its entire-clip SD download path is **not** release-acceptable.
2. Use the existing `RollingHlsSink`/`RollingMuxedHlsSession`/
   `RollingHlsWorker` staging design where it helps, but make the
   network-to-file transport **low-RAM** instead of buffering whole segments.
3. Resolve the proven native decoder-FD lifetime issue. The current M4
   worker correctly forbids unsafe retirement while libmpv holds media;
   **this alone is not enough** for long-video playback because a 25 MB
   cap will otherwise eventually stall it. Advance the playlist and unlink
   old files only with validated decoder-safe ownership/eviction behavior.
4. Test long and high-bitrate videos: startup before full download, bounded
   RAM peak, bounded SD peak, no playback stall after a full rolling window,
   selected audio and video, pause/seek/sleep/wake, repeated viewing.
5. Verify the hard combined 100 MB SD ceiling while playing and *zero
   disposable media* on stop and after restart. Run normal and ASan/UBSan
   host regressions, devkitA64 builds and physical Switch checks.
6. Do not merge PR #8 or claim the new pipeline accepted before those
   checks pass. No ads, no Shorts and no tracking-heavy share links.

The user-provided estimate of about 8 MB of available RAM for competing
sysmodules is a constraint to investigate on their setup, **not a
hardware-verified free-memory measurement** or a guarantee that video
decoding could run in under that amount. Optimize RAM use and measure
instead of assuming any universal Switch free-RAM number.
