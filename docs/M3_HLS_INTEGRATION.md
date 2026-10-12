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

## October 11 — real HLS + external AAC source proof

GitHub's independent visionOS guest source check **passed** for
`dQw4w9WgXcQ`:

- Actual Googlevideo HLS **MPEG-TS H.264** elementary stream bytes retrieved.
- HLS was returned as a **direct `hls_variant` media playlist**, not the
  older expected `hls_playlist` master URL. Source and local cache now
  support both without inventing metadata.
- The video segment had **no AAC track**. The same validated visionOS player
  response provided a separate signed **`audio/mp4` AAC** stream. Actual
  MP4 media bytes were retrieved in the reference proof.
- The two other sampled user video IDs were guest-playability blocked in the
  GitHub environment, so there is **no general-playback reliability claim**.

Native integration now uses `parse_m3_guest_hls_source` to select a reviewed
same-response AAC format, `MediaChunkReader` over dedicated Horizon SSL for
bounded HTTP-206 audio ranges, and `HlsCacheJob` to download full AAC MP4
into the **same pending SD transaction as HLS video**. The cache does not
publish when AAC is missing, incomplete, malformed or HTTP-blocked.
`SwitchHlsCacheSink` stores the completed file as the local-only
`ready/external_audio.m4a`, and the isolated mpv player attaches that file
via its local `audio-file` option. HLS MPEG-TS segments use `.ts` names;
fragmented MP4 with `EXT-X-MAP` uses `.m4s`.

This is backed by host tests for real file staging, complete ranged audio
transfers, 403 abort, truncated AAC rollback, MPEG-TS PMT detection, and no
token URL leakage.

**2026-10-11 hardening:** cache publication now requires all expected HLS
segment indices and initialization pieces, adds an atomically committed
`audio_mode.txt` marker, and records the exact complete external AAC byte
length as `external_audio.bytes`. The player rejects a missing or changed
AAC and checks selected audio tracks after mpv playback restarts. A copyright-free
FFmpeg CI fixture generates real H.264 MPEG-TS + AAC MP4 and fully decodes
the paired local tracks with file-only demux access. This demonstrates actual
host FFmpeg demux compatibility, **not** validation of the Switch libmpv
`audio-file` option or hardware A/V synchronization. The normal guest/client networking contract is unchanged.

**Still unverified on hardware:** local mpv external-audio option support in
the pinned Switch libmpv, audible AAC sync across seeks and sleep/wake,
full-video transfer throughput, startup delay, and release-quality playback.
The full-clip SD cache is intentionally conservative and not yet a streaming
buffer suitable for arbitrarily long videos.

**CI note:** the legacy progressive yt-dlp reference was retained only as
manual workflow_dispatch. The active independent gate is
`m3-vision-hls-live.yml`, which exercises actual visionOS/HLS and separate
AAC media bytes. Successful host and Switch compilation are separate from
owner hardware acceptance. NO automatic integrated-NRO publication.

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

### External AAC option correction

The headless host libmpv integration test now requires a real libmpv
installation, loads the generated local HLS file with
`protocol_whitelist=file`, uses the same `audio-files-append` option
through `mpv_set_option_string`, asserts a selected audio and video
track, and exercises seek, pause, stop and replay. This is a stronger
functional test than inspecting FFmpeg bytes alone; the host library
does not replace Switch-specific libmpv 0.36 or hardware testing.

The pinned mpv 0.36 documentation marks `audio-file` as a **CLI/config-only
alias**, not a reliable libmpv API option. The native player now sets
`audio-files-append` directly before `mpv_initialize`. The append operation
accepts one literal path, preserving the `sdmc:` prefix instead of splitting
it on the Unix path-list separator. The runtime selected-audio-track check
remains mandatory. This is source-backed mpv option correctness, not a claim
of physical Switch playback acceptance.

### Second cache regression tranche

A second set of range-transfer tests covers cancellation during a multi-chunk
AAC download, HTTP 403 on a later AAC chunk, clean retries following a failed
cache transaction, missing AAC range-transport rejection before video download,
invalid requested media identity, unsafe AAC hosts, and source/playlist
duration drift. Video-only HLS is no longer permitted simply because the
origin returned a master playlist: audio must be verified in MPEG-TS metadata,
supplied through an HLS audio rendition, or fully fetched through a reviewed
external AAC source. All checks are fail-closed with no ad-player fallback.

### 2026-10-11 verified libmpv configuration correction

A real libmpv host test exposed `MPV_ERROR_OPTION_NOT_FOUND (-5)` for the
previous pseudo-option `audio-files-append`, despite native compilation
succeeding. The API now sets the real `audio-files` option with
`MPV_FORMAT_NODE` containing one `MPV_FORMAT_NODE_ARRAY` of local filename
strings. This preserves the literal Switch `sdmc:` path and avoids colon
path-list parsing. The CI test also insists that mpv actually discovers and
selects an audio track; build-only success does not count.

### Playback failure handling

M3 now labels guest transport errors, HTTP refusals, HTTP-200 playability
rejections, media playlist errors and missing AAC separately. The Search UI
shows actionable plain language instead of stage integers. Download,
SD-write, cache-size and atomic-publish failures also map to safe messages.
Neither signed URLs nor guest tokens enter user-visible diagnostics.

### Direct MPEG-TS codec gate

Direct `hls_variant` playlists often omit codec metadata. Their cached
MPEG-TS video segments must now expose a PAT/PMT-advertised H.264 stream
before standalone AAC transfer and cache publication. Corrupt transport
bytes or an unsupported direct codec fail before costly separate audio
download. This is metadata validation only; the real FFmpeg/libmpv decoder
gates remain required. Fragmented MP4 sources retain their existing path.

### Large-clip transfer visibility

Before progressive buffering exists, M3 continues to require a complete,
bounded cache. The Search screen now distinguishes HLS segment download from
external AAC download and displays completed/total segments plus downloaded
KiB. This makes lengthy transfers visibly cancellable without claiming that
the full media is already playable; full playback still starts only after
atomic video+audio publication. No network or decoder permissions are widened.

### Longer headless AV decoder gate

The synthetic local clip has been extended to 45 seconds. Host libmpv now
receives a filename with a literal colon via the typed audio-files array,
seeks to 30 seconds, resumes and checks that H.264 and AAC decoders advance
after the seek. Stop and replay must yield a new FILE_LOADED event.
This is a host software-decoding gate, not Switch audio-output acceptance.

### Correct numeric playback-position assertion

The 45-second host libmpv probe initially failed after seeking because its
test converted a display-formatted `time-pos` string into a Python float.
The test now reads `time-pos` with `MPV_FORMAT_DOUBLE`, matching the
documented native API, and includes the last H.264/AAC and position state in
failure diagnostics. No production decoder behavior was bypassed.

### Native libmpv asynchronous audio readiness

The integrated Switch player previously treated one
`MPV_EVENT_PLAYBACK_RESTART` without an immediately selected AAC track
as a fatal silent-video condition. That event can arrive before asynchronous
track selection has stabilized. The player now uses a pure, host-tested
4-second readiness gate: once a local HLS file loads, AAC track selection is
rechecked on successive UI ticks, early unselected states remain pending,
and a sustained missing track stops playback without falling back to an
ad-bearing web player. Stop, replay and seeks reset the gate; fatal audio
errors are preserved in the UI rather than being overwritten by the
subsequent expected stop event.

### Sleep/wake readiness resilience

A monotonic wall-clock timeout can falsely reject asynchronous AAC if the
Switch sleeps between media-load and the next player tick. The readiness
gate now accumulates observed active UI time, limited to 100 ms per
observation. A long suspend counts as at most one bounded tick, while
roughly four seconds of active missing-audio observations still fail
closed. Host regressions cover clock regression, long suspend, delayed
AAC selection, failure latching and stop/replay reset.
