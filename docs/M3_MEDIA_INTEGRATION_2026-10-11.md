# M3 media integration — larger development tranche

## Owner-accepted baseline

**Do not reopen:** offline player NRO built from
`2acc3b7533d0e1ede0db0517b085ffd1179cb372`. On real Switch, the owner
accepted video, H.264/AAC, `nvtegra-copy`, two-row controls, pause/resume,
seek backward/forward, stop/reload, the louder audio fixture and sleep/wake.

This tranche moves *forward* on draft PR #8. It neither merges into M2 nor
alters M2's exact `www.youtube.com:443` before-DNS allowlist.

## Implemented in this tranche

### 1. Signed CDN media policy (host core)

`include/tizentube_nx/playback/media_chunks.hpp`,
`src/core/media_chunks.cpp`, `tests/media_chunks_tests.cpp`.

- Accept only strict HTTPS signed `*.googlevideo.com/videoplayback` sources.
- Use GET with a strict 1 MiB maximum Range chunk, identity encoding, and
  no arbitrary caller-provided headers or cookies.
- Require HTTP 206 plus exact, bounded Content-Range/Content-Length and
  correct media MIME. Reject 200 unbounded downloads, redirects, HTML,
  gzip/chunked/duplicate headers, malformed/truncated or inflated ranges.
- Allow the standards-compliant shorter *final* 206 when Range goes past EOF.
- Never emit signed URLs or origin error bodies in user diagnostics.

### 2. Progressive media caching and cancellation

`media_download.hpp/.cpp` and host tests.

- Repeated, bounded one-range-per-pump transfer, <=512 MiB total.
- A staged `MediaChunkSink` publishes only once fully downloaded. The
  coordinator detects size changes between ranges and aborts partial data.
- Abort/cancel/restart supported; clear ephemeral signed source URL on finish.
- On Switch the M3-only `SwitchMediaCacheSink` writes to
  `sdmc:/switch/TizenTube-NX/media-cache/active.mp4.part`, then flushes and
  renames to `active.mp4`. A failed transfer removes only the disposable
  partial, never touches source saves or other applications.

### 3. Real Switch SSL media adapter, isolated behind a feature flag

`LibnxMediaChunkTransport` in `switch/source/libnx_http_client.*`.

- Compiled only with `TTNX_M3_MEDIA_EXPERIMENT=1` /
  `TTNX_M3_MEDIA_PLANE`. Default browsing NRO remains in its exact M2
  deny-by-default network lane.
- Reuses proven libnx BSD sockets/Horizon SSL and performs an independent
  strict media-host check again immediately before DNS.
- No following redirects, no arbitrary headers or POST, no media player
  internal network transport bypass.
- CI: `M3 experimental media transport compile` verifies Switch ABI without
  publishing a misleading playback NRO.

### 4. On-console guest YouTube source preparation

`youtube::make_player_request` reuses the approved WEB guest context and
`www.youtube.com/youtubei/v1/player` host. No password, cookie import,
OAuth, third-party server, adSignalsInfo or tracking extras.

`GuestDirectExtractor` validates response identity/playability/duration,
ignores ad/tracking trees, filters Shorts and live, and retains only direct
reviewed H.264/AAC MP4 stream URLs. URL cipher, JS signature deciphering,
DRM, unsupported codecs and unsafe CDN destinations are explicitly rejected.
Unknown streaming formats fail closed.

`ProgressivePrepareJob` connects the extraction result, existing selector,
secure range reader and atomic cache. The first hardware candidate selects
a *muxed* H.264/AAC MP4 up to 720p30, avoiding silent audio loss from
adaptive-only streams. No ad-bearing YouTube web player fallback exists.

Offline test suites exercise malformed/hostile JSON, privacy requirements,
native selection, signed media host checks, range/header validation,
multi-part transfers, safe cancellation and end-to-end preparation.

## What is not claimed

**This is not yet an actual YouTube streaming NRO.** Network media access is
disabled in the accepted default Switch binary. The M3 media transport is
separately compiled but is not yet wired to a Borealis background worker or
the hardware-accepted mpv frame/audio lifecycle.

YouTube can require client signature deciphering / PO-token support, which
the direct WEB extractor does *not* silently defeat. If a real video offers
only ciphered/adaptive formats, it will currently fail to resolve or be
unavailable under the conservative progressive selector.

Longer real YouTube playback, seeking, expiry/refresh, CDN TLS/responses,
adaptive A/V sync, captions, SponsorBlock integration and real ad-free
hardware acceptance remain open.

## Next integrated development goals

1. Run guest `/player` resolution explicitly from selected normal Search
   result, on a background worker with stale-generation/cancel handling.
2. Connect the opt-in network media transport and SD cache to the working
   player, with fully bounded storage and no on-UI-thread networking.
3. Test a *real normal YouTube video* end-to-end on hardware as one combined
   NRO candidate, not a long series of minor probe releases.
4. Add expiry refresh, adaptive video/audio and legitimate unsupported-
   source diagnostics before advertising 720p/1080p or general compatibility.
5. Independently review media DNS/TLS and ad/SponsorBlock requirements.
   No Shorts anywhere, no credentials, no ad-enabled web fallbacks.

GitHub is authoritative. Preserve later commits; never reset/rebase backward
or force-push. Do not merge draft PR #8 until the owner authorizes it.
