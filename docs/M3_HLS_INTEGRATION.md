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

### Efficient separately signed AAC acquisition

For the real-world direct HLS video-only path, the cache now validates the
video init and first media segment, then fetches the independently signed
AAC MP4 **before** downloading the remainder of the video. The old ordering
waited until all HLS video segments were downloaded to find out that
external audio was blocked or expired. An early AAC 403 now rolls back
with at most one video media segment and its optional init downloaded,
without wasting a full clip. The cache still publishes only after every
remaining H.264 video segment and the complete AAC are present. A TS
codec mismatch still fails before opening the AAC URL. Tests verify the
transfer ordering, cancellation, late failures and no premature publish.

### Live paired A/V decoder gate (network-isolated from FFmpeg)

The VisionOS source CI now includes an opt-in `--decode-real-av` path.
For the first public guest video with an eligible direct HLS video-only
MPEG-TS rendition and a signed separate AAC MP4, it fetches **one entire
bounded video segment and the complete bounded audio resource** (maximum
16 MiB each). Every HTTPS URL is independently checked under the established
media-host policy with redirects refused. Both files exist only in an
ephemeral private temporary directory; the workflow never uploads any
media, source JSON or signed URLs. Host `ffprobe` must identify actual
H.264 and AAC streams, and `ffmpeg` must decode the independent tracks
and open them together using a file-only protocol whitelist. The CI
result remains an **off-console media compatibility proof**, not a
Nintendo Switch playback or audio-synchronization acceptance.

### Verified AAC MP4 container boundaries

The core HLS downloader now checks the complete independently signed AAC
file through an incremental, constant-memory ISO BMFF top-level atom parser.
A trustworthy `ftyp` header alone is no longer enough: the entire 206
sequence must form bounded, non-overlapping atoms with `ftyp`, `moov`,
and `mdat`, and the declared resource length must be accounted for exactly
before fetching the rest of the video or publishing any cache. This rejects
truncated or bogus responses that previously could pass an MP4 signature
test. It is **container validation**, not a substitute for FFmpeg decode
or independent physical Switch A/V acceptance.

### Native-parity AAC acquisition in the LIVE decoder proof

The live real-A/V proof now uses the **same contiguous 1 MiB HTTP 206 Range
sequence** as the native AAC transport. A full un-ranged GET could produce a
misleading success/failure that would not reproduce on Switch. Each chunk
must have a complete non-overlapping `Content-Range`, an exact body length
and the same <=16 MiB total size; redirects, unexpected HTTP 200, partial
or changing resources are rejected without disclosing signed URLs.
Offline mocked-transport tests cover multi-range reconstruction and
discontinuous responses.

### Live native-parser compatibility

The LIVE CI gate now compiles a standalone host executable using the **same
C++ `inspect_mpeg_ts` implementation and incremental `AacMp4Structure`
gate as the Switch application**. It runs these checks directly on the
temporary real YouTube segment and AAC file before invoking the file-only
FFmpeg decoder. A host FFmpeg success can no longer mask a native
parser rejecting genuine media. Successful runs emit the privacy-safe
`M3_NATIVE_MEDIA_PARSERS=PASS` category; no signed URLs, media or binary
artifacts are uploaded. Nintendo Switch decoder/runtime behavior is still
a separate hardware acceptance step.

### Real external AAC attachment in libmpv

The LIVE gate now goes beyond independent FFmpeg codec decoding and the
native C++ container checks: it creates an ephemeral single-segment,
local-only HLS master playlist for the actual downloaded YouTube TS
segment, configures host `libmpv` using the **same typed
`audio-files` node-array API as the Switch player**, and requires
an actual selected AAC track, recognized H.264/AAC codecs and a moving
playback clock. A successful run emits `M3_REAL_LIBMPV=PASS`. Network
protocols are restricted to `file`, video/audio output uses null
sinks, and no media or URL artifacts survive the temporary directory.
Host `libmpv` is not the pinned Switch library and does not prove real
console rendering or speaker output.

### Real published Switch-cache integration (no simulated playlist)

The live H.264/AAC proof now publishes the bounded, downloaded public
YouTube media through **the actual production `SwitchHlsCacheSink`**
compiled for the host test directory. It verifies that the app's own
`pending → ready` transaction creates `v_00000.ts`, native
`master.m3u8` and `video.m3u8`, `external_audio.m4a`,
`audio_mode.txt`, and an exact AAC-size sidecar. Only then does host
libmpv open the **native-generated** local master playlist and select
the committed AAC. The earlier handcrafted playlist no longer hides
bugs in production manifest creation. No signed URL is embedded in the
published media files. The disposable host cache is deleted after
verification; this does not test actual Switch hardware.

### Transient video and AAC transport resilience

The integrated worker now retries dropped media sockets and HTTP
429/500/502/503/504 responses **at most twice for each same signed segment
or AAC byte range**, with 250/500 ms worker-only delays. Each `pump()`
still makes at most one bounded HTTPS request. Video/AAC progress advances
only after a validated transfer is persisted. Permanent HTTP 403,
unsafe URLs, malformed media/headers and failed SD writes never retry.
A failed retry sequence aborts staged media and preserves the previous
published clip. Host regressions cover one-off socket drops, AAC throttling,
permanent refusals and exhausted 503 budgets. An expired signed URL
still requires a fresh authorized source; no token bypass is attempted.

The ranged-AAC transport also preserves the numeric HTTP status when its
socket adapter reports `!ok`. This prevents a backend-reported 403 from
being misclassified as a dropped connection, while allowing a bounded
retry on an actual 503. Numeric status is never accompanied by the
signed CDN URL in the UI or logs.

### Playlist-stage CDN resilience

The first signed master/variant/media playlist fetch now shares the
same fail-closed bounded retry treatment as the segment downloader.
One dropped connection or HTTP 429/500/502/503/504 can be retried twice
on the media worker, with bounded 250/500 ms pacing. A signed HTTP 403
or redirection is never retried or followed; bad MIME, broken duration,
missing media or forbidden manifest tags fail immediately. The YouTube
guest browsing client and its strict www.youtube.com policy are unchanged.
Host tests exercise recoverable 503, persistent 429 and terminal 403.

### First-segment rejection of silent direct HLS

Direct MPEG-TS VOD without a signed external AAC file or a declared HLS
audio rendition now checks muxed H.264 and AAC in its very first
segment. Silent/unrecognized streams abort immediately without
downloading the remaining movie; valid muxed audio and real YouTube
video with a separately verified AAC file remain supported. The
previously accepted complete cache remains preserved.

### Storage-aware complete-cache rendition selection

For HLS master playlists with multiple compatible variants, the
resolver now estimates complete-video bytes using advertised peak
`BANDWIDTH` and duration. It reserves 20% of the 512 MiB cap for
audio and variability and chooses the best variant likely to fit,
rather than always selecting 720p even for hour-long videos.
Unverifiable high-duration bitrates fail early, rather than wasting
the entire download. The existing hard byte limits remain mandatory;
the estimate is not a proof that a stream will fit. Direct-media
playlists without a master are not artificially assigned a bitrate.

### Streaming startup buffer planning

A new host-tested, independent `HlsStartupBuffer` prepares the
bounded 12-second / six-segment prefix needed for future progressive
HLS playback. Readiness requires in-order video segments, a verified
H.264 source, and complete AAC; no partial cache is currently exposed
to the native player. It is a preparation contract, not an assertion
that Switch libmpv can refresh a growing HLS event playlist.
The accepted full-cache transaction remains unchanged.

### Downloader integration of startup-buffer readiness

`HlsCacheJob` now feeds the tested startup-buffer tracker from
real, successfully persisted video segment completions and
independently completed AAC downloads. The verified TS codec is
required. The tracker does not modify the SD-cache transaction
or permit opening any unfinished media in libmpv; it provides
an auditable readiness state for the subsequent append-only
streaming milestone. Existing automatic full-cache playback
and user hardware acceptance are unchanged.

### Append-only local HLS EVENT snapshots

A separate host-tested `make_hls_event_snapshot` renderer now
builds strictly local EVENT playlists from verified persisted
video segment indices, preserving a stable media sequence and
adding `#EXT-X-ENDLIST` only for the completed clip. Real signed
YouTube URLs are NEVER copied from the input plan. The renderer
rejects missing fragments, premature ENDLIST, malformed durations
and MPEG-TS / fMP4 extension mismatches. This prepares the
append-only local timeline needed for playback during buffering,
but does not yet enable libmpv to open a growing pending SD
directory. That requires proving atomic replacement, active
directory lifetime, and refresh/seek behavior first.

### Host event-playlist refresh experiment

A new synthetic H.264/AAC host integration checks whether a
**running** libmpv instance follows an append-only, atomically
replaced local HLS EVENT playlist. It initially exposes two
three-second segments, then adds four more while playback is
already underway; the test requires the media clock to advance
beyond the original six-second window without reopening libmpv.
This is a host feasibility test, not a console implementation.
The production app still waits for full-cache publication until
Switch-specific buffer lifetime, seeks and AAC synchronization
are accepted.

### Native SD event snapshot staging (not yet player-visible)

The production `SwitchHlsCacheSink` can now write an atomic,
local-only `pending/video_event.m3u8` snapshot after a verified
12-second H.264/AAC startup prefix has actually been persisted.
The downloader records whether the optional snapshot was staged,
but **continues fetching every remaining segment** as before.
The staged playlist uses a fsynced temporary file and rename;
missing segments never replace the previous snapshot. The final
full-cache commit strips the EVENT snapshot before renaming
`pending` to `ready`, and abort cleans up its known scratch
filenames. This means existing complete playback cannot mistake
a partial EVENT for a finished video. Host tests cover atomic
growth, missing segments, local-only URLs and final cleanup.
Switch live streaming remains disabled until stable active
directory lifetime and incremental libmpv refresh are validated
on device.

### Growing staged event timeline and hardware progress

The HLS worker now refreshes its private, atomically renamed
`pending/video_event.m3u8` after **every newly persisted video
segment**, rather than creating only one startup snapshot. Missing
segments never enter the playlist, a failed optional snapshot cannot
replace a complete cache, and the final VOD publication still strips
the pending event file. The Search page now reports the verified
startup-buffer duration while it safely completes the full cache,
instead of implying early streaming is already enabled. Tests assert
the staged timeline reaches the final number of segments.

### Exact HTTP body framing on Switch TLS sockets

The Horizon SSL media transport now stops reading immediately when
a complete, unambiguous bounded `Content-Length` response has
arrived, rather than always waiting for the server to close its
connection. This is especially important for repeated signed HLS
segments and HTTP-206 AAC ranges. The shared framing parser rejects
oversized headers, conflicting lengths and Transfer-Encoding plus
Content-Length ambiguity. Chunked replies retain the existing
validated EOF/parser fallback. The strict destination whitelist,
TLS certificate checks, 16 MiB media cap and no-redirect policy
remain unchanged.

### Stable playback lease for the completed SD cache

The integrated libmpv activity now owns an exclusive lease on the
published `ready/` HLS directory from player initialization until
`mpv_terminate_destroy` has closed its file descriptors. A new
download or publish cannot rename/delete this directory while it
is in use; attempts leave the completed clip intact. The lease
does not authorize playing unfinished `pending/` files and
does not alter the M2 browser. Host cache regressions cover
exclusive acquisition, denied replacement, cleanup, and recovery
after closing the player.

### Owner-requested major-session NRO packaging

The integrated M3 pipeline now retains one experimental Switch NRO
artifact for each forward-only development-session **final push**
on the isolated M3 branch, complete with SHA-256, exact commit and
a hardware checklist. It is **not** merged into the accepted M2
branch, not released on the main front page, and not represented as
hardware-proven merely because the CI build passed. The assistant
shares the latest completed green run's artifact rather than issuing
an NRO after every individual fix. The automatically generated
artifact expires after 14 days. M2 networking, Shorts exclusion
and zero-ad/web-player policy remain unchanged.

### Real user-reported manifest failures (diagnostic lane)

The 2026-10-11 device screenshots of three normal public videos
(`-CuQnpJ4sPg`, `YJABBgpaNts`, `0NktpcjsK58`) all hit the
preplayback **ManifestRejected** stage. A new CI diagnostic inspects
the guest VisionOS response and first HLS playlist from those exact
video IDs, printing only fixed category codes (MIME type family,
master/media/CRLF/format class, or HTTP category). It does not print
or retain signed URLs, visitor identifiers, response JSON, or media.
This diagnostic is deliberately non-gating: CDN restrictions on a
GitHub runner are not the same as the user's Switch network.

### Ordinary-video HLS compatibility hardening

The native HLS parser accepts standards-conforming CRLF, case-insensitive
MIME types and plaintext only when a genuine #EXTM3U playlist passes all
the existing strict HLS validation. AVC-only video masters are now permitted
only with an independently approved signed AAC URL from the same guest
player response or an approved HLS AUDIO group. Forbidden hosts, AES keys,
ad-cue tags, AV1/HEVC and Shorts remain denied. The generic manifest
failure is split into MIME, body, length and master-specific statuses
to diagnose the precise stage on hardware.

### Second native route after unsupported HLS

The integrated M3 media worker now attempts a separately authorized
**muxed H.264/AAC MP4** through the already reviewed
`GuestDirectExtractor → ProgressivePrepareJob → MediaDownloadJob`
path when (and only when) a normal video's HLS manifest/variant fails
compatibility checks. It preserves independent HTTPS Googlevideo
policy, requires the same video ID, normal non-Shorts and guest
playability, refuses DRM/cipher-only formats, performs bounded ranged
media transfer into a disposable SD cache, and opens the completed
`active.mp4` through the existing native player. Guest restrictions,
403s, unsafe identity and transport failures do not trigger a
different playback profile as a bypass. Download progress clearly
identifies direct MP4 rather than HLS. The feature does not guarantee
that YouTube offers a safe muxed MP4 for every video; if it does not,
the app reports that category honestly instead of claiming success.
