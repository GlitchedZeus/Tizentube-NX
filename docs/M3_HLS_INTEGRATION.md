# M3 HLS Playback Track — Real-Video Integration

**Status:** experimental source code; NOT a user-ready YouTube playback NRO.
**Owner's accepted hardware baseline:** offline H.264/AAC libmpv,
louder audio, pause/resume, ±10s seek, stop/reload, sleep/wake passed.
**User hardware report:** WEB `/youtubei/v1/player` returned HTTP 200,
but `playabilityStatus` blocked normal YouTube videos. Search works.

## Why the architecture changed

A guest Search result is **not** a playable source. The original design
mistakenly assumed the anonymous WEB client would yield one directly
downloadable H.264+AAC MP4 for most videos. Physical hardware disproved
that premise on several normal videos.

The new isolated M3 path uses a separately reviewed **visionOS guest
player-profile request**, supported by the M2 guest's original server-issued
visitor data. This is a private YouTube protocol, not guaranteed to work
in all regions or over time, and does NOT automatically authorize playback
if upstream responds with `LOGIN_REQUIRED` or a protected source.

Implementation: `make_m3_visionos_player_request`,
`parse_m3_guest_hls_source`, and `VisionHlsSource`.

## Implemented source-to-local-media pipeline

1. **Source identity** — canonical normal video ID only; never accept
   Shorts URL input; validate playability, duration, video ID and reviewable
   signed HLS master URL before accessing the media network lane.
2. **HLS master parsing** — parse bounded manifests; prefer H.264/AAC up
   to 720p30; bind optional explicit AUDIO renditions; reject invalid/unsafe
   hosts, DRM/keys, ad break markers and non-VOD features we cannot handle.
3. **Segment playlist parsing** — finite VOD only, with strict limits on
   segment count, duration and init URLs. Reject unsupported byte-range,
   discontinuity and encrypted media instead of guessing.
4. **Native HTTPS** — experimental Horizon SSL HLS playlist client (1 MiB)
   and signed HLS segment client (16 MiB) with distinct MIME expectations.
   Both repeat destination checks before DNS and refuse redirects. The
   M2 browsing network policy is unchanged.
5. **Clip source verification** — `HlsSourceProbe` resolves the normal
   video, HLS master, selected video playlist, and separately downloaded
   AAC audio playlist. It checks A/V durations for consistency and does
   not treat a manifest alone as playable media.
6. **Cancellable media transfer** — `HlsCacheJob` fetches one bounded
   segment per pump, rejects any failed part, enforces a 512 MiB total
   limit, and never publishes half a video/half an audio track.
7. **SD cache transaction** — `SwitchHlsCacheSink` writes media only in
   the app-owned `sdmc:/switch/TizenTube-NX/media-cache/hls/` directory.
   It creates strictly **local** relative HLS playlists and publishes
   `ready/` only after both tracks and playlists are complete. A
   `previous/` directory preserves the last good clip through an
   interrupted swap. No signed CDN URL is copied into the local playlist.
8. **Player** — the experimental Search action now routes through the
   HLS source/cache flow and invokes the existing 640x360 libmpv player
   with the completed local `ready/master.m3u8`. mpv network access is
   not expanded. Original offline MP4 loading and controls are preserved.

## What has actually passed?

- **Already physically accepted:** offline video player, H.264/AAC, audio,
  controls, sleep/wake (separate accepted NRO).
- **Host-only evidence:** fixture playback selection, source validation,
  HLS master/segment validation, adversarial transport cases, staged
  video/audio cache success, cancellation and publish-failure cleanup.
- **Native compilation evidence:** monitored separately in PR #8; this
  is necessary but never sufficient for hardware playback.
- **Real HLS YouTube source:** not yet independently verified through the
  native Switch network lane.
- **Local HLS mpv demux + audio sync:** not yet physically verified.
- **YouTube end-to-end hardware video:** NOT ACCEPTED.

GitHub Actions live reference currently cannot extract the selected public
videos from its cloud runner (0/2). This is evidence of a restricted
external test environment or changing client access; it neither proves
that the Switch path succeeds nor that it will fail. Do not skip these
live gates based on simulated fixtures.

## Release gate — no more one-fix/one-NRO testing

The owner explicitly does not want another NRO until there is a credible
real-video path. PR #8 remains **DRAFT**, unmerged; the automatic experimental
NRO artifact upload is disabled.

Before any proposed hardware candidate:

- Source ID, guest player response and all HLS URLs pass host/identity tests.
- At least one real normal YouTube source has demonstrated media bytes
  through the same source/transport strategy, not just HTTP 200 metadata.
- HLS master, video and audio tracks validate and finish in one test
  invocation; no signed URLs, credentials or tracking are logged.
- Native compilation, host sanitizer/regressions and independent outbound
  security checks pass.
- A controlled hardware test demonstrates first frame, audible AAC,
  >10-minute stable playback, pause, seeks, replay, cancellation,
  sleep/wake, and B returning to the same Search selection.
- If a title is unsupported/restricted, report a non-sensitive, bounded
  explanation and cleanly return to Search. Never play Shorts, ads,
  promoted content or fall back to an ad-bearing web player.

Transport/client implementations are subject to YouTube private-protocol
changes. It is not technically possible to promise that the external
service will never issue HTTP 4xx or an invalid stream. The project
must handle those outcomes safely, and must not ship a candidate based
only on a successful compiler run.

Source reference: Switch-NewPipe's reviewed GPL-3.0 media protocol research,
documented in `docs/THIRD_PARTY_MEDIA.md`. The imported UMP framing code
is separately attributed. No historical upstream result is claimed as
TizenTube NX hardware acceptance.
