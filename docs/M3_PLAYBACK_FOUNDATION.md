# M3 playback foundation — 2026-10-11

## Position and acceptance

**Implementation exists; real YouTube playback does not.** This branch is deliberately
stacked on the latest physically accepted M2 source. It is not a finished player
and no NRO should be advertised as able to stream videos merely because these
new core files build. PR #6's real-device gates remain authoritative.

The first pass delivers the subsystem boundaries and risk controls needed for a
future libmpv integration:

1. \`Extractor::resolve(video_id)\` returns typed audio/video format candidates.
2. \`select_streams()\` validates candidates and chooses a supported video/audio
   pair or a muxed fallback. The chosen pair is \`SelectedStreams\`.
3. \`PlayerBackend\` receives **only** selected media sources. No arbitrary web
   navigation or YouTube ad-player fallback exists in the API.
4. \`PlaybackSession\` owns start/pause/resume/seek/stop state, including explicit
   error states and live-stream seek rejection.
5. \`SponsorBlockEngine\` independently decides Auto/Ask/Off actions based on
   normalized timed segments. It never controls playback or network IO directly.
6. \`tools/extractor_service.py\` proves a bounded yt-dlp-backed extraction
   adapter can produce the same shaped formats. It is **loopback-only** and
   deliberately not wired to the Switch. The current M2 network client cannot
   connect to it.

## Native compatibility profile

Initial policy:
- H.264 / AVC in 8-bit SDR through 1080p60.
- Prefer a separate AAC audio stream; Opus can be represented, hardware test
  pending. A compatible muxed audio+video source is a fallback.
- VP9 is disabled until hardware acceptance. Even when explicitly enabled,
  compatible H.264 remains preferred.
- AV1, HDR, DRM and high-bit-depth video do not silently fall through to an
  unverified decoder.
- Missing or unsafe URLs fail selection, not silently retry with an unrestricted
  alternative player.
- Every candidate URL is HTTPS, \`/videoplayback\`, under a **proper subdomain
  boundary** of \`googlevideo.com\`, without userinfo, IP literals, fragments,
  alternate ports or control characters.

These URLs are structurally *reviewable*, **not network-authorized**.

## Hard network boundary

**Do not modify or weaken M2's allowlist**:

\`www.youtube.com:443\`

The current libnx transport remains responsible for pre-DNS policy enforcement.
Future M3 media transport requires separate policy review and implementation,
including:
- suffix-boundary checks for approved Googlevideo hosts;
- Nintendo hard-deny first, independent of 90DNS;
- TLS peer identity/date/CA verification;
- per-redirect review before DNS, no arbitrary CDN URL fallback;
- an explicit source of trusted media hostnames;
- signed URL expiration, range reads, reconnects and bounded throughput;
- no credentials, tokens or signed URLs in logs/diagnostics;
- independent SponsorBlock endpoint review.

**Do not bypass this by feeding URLs to mpv until its own transport follows these
rules.** mpv's native HTTP stack is not automatically governed by our existing
\`LibnxHttpClient\`.

## Development-only resolver

\`tools/extractor_service.py\` has an intentionally narrow contract:

- \`GET /healthz\`: readiness without fetching YouTube.
- \`POST /v1/resolve\`: JSON \`{"video_id":"IzJ7R4EnYmI"}\` only.
- Bearer token in \`TTNX_EXTRACTOR_TOKEN\` of at least 32 characters.
- Listens **only** on \`127.0.0.1:8765\`. No option to expose it to LAN.
- Accepts no URLs, host overrides, cookies, passwords or custom headers from
  clients. Inputs are strict 11-character IDs.
- \`yt-dlp\` must be independently installed, kept current and configured with
  any legitimately required EJS / PO-token plugins.
- Formats are bounded and normalized, and unrelated hosts are excluded.
- No verbose HTTP request logs or reflected extraction errors.
- Signed Googlevideo URLs are short-lived bearer-like secrets and should not be
  logged or persisted.
- No Google account or cookie import is part of the interface.

For local research:

\`\`\`bash
python3 -m pip install --upgrade yt-dlp
export TTNX_EXTRACTOR_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(40))')"
python3 tools/extractor_service.py
\`\`\`

Test without live network:

\`\`\`bash
python3 -m unittest discover -s tests -p extractor_service_tests.py -v
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
\`\`\`

Because the endpoint uses loopback-only HTTP, **it is not acceptable to connect
directly from a Switch or expose it as an internet-facing service**. Adding a
TLS-authenticated proxy or designing native extraction requires a separate
security and device-integration review. A local resolver requiring another
computer is a development fallback, not the final standalone-console UX.

## Next hardware milestones — in order

1. Build a separately opt-in, locally packaged Switch libmpv + FFmpeg +
   Deko3D/nvtegra test player with a **known legal offline MP4** or explicitly
   allowlisted test fixture. Prove hardware decoder, A/V output, seek, pause,
   stop/re-enter, dock and undock with no YouTube extraction in this gate.
2. Plug in \`PlayerBackend\` around the proven renderer. Keep the existing M2
   browsing UI and network workers untouched.
3. Prove separate adaptive video and audio playback; verify synchronization and
   quality selection. Check behavior for dropped audio streams.
4. Determine a **standalone-console** stream-extraction strategy, using the
   self-hostable prototype only as a development oracle. Do not quietly make
   a permanently running PC a release requirement.
5. Implement a reviewed media transport, including Googlevideo redirect/range
   policies and certificate validation. Real YouTube public playback only
   begins once both source validation and transport review are complete.
6. Add SponsorBlock's public API client, normalized response fixtures, user
   category settings, seek integration and visible Skip affordance.
7. Prove on real Switch: long 1080p playback, pause/seek/resume, 60 fps,
   separate audio/video, network expiration/reconnect, app suspend/resume,
   no deliberate platform-served ads, no Shorts, no Nintendo DNS/traffic.

A development-only player that deliberately plays YouTube ads does **not**
satisfy the project's M3 acceptance contract. Every hardware result should be
recorded with exact commit, CI run, NRO hash, observed behavior and pass/fail.

## Security and regression tests

\`tests/playback_foundation_tests.cpp\` tests source validation, preference rules,
codec restrictions, malformed/hostile URLs, Nintendo/M2 deny policy, Shorts
rejection, fallback and player state transitions, plus SponsorBlock overlaps
and skip suppression.

\`tests/extractor_service_tests.py\` runs entirely offline, including a
loopback HTTP server with a mocked yt-dlp extraction response. It tests strict
authentication, request size/schema, identity binding, Shorts rejection,
hostile media hosts, response cache headers and error redaction.

See also \`docs/M3_PLAYBACK_EXTRACTION_RESEARCH_2026-09.md\` for detailed
technology selection and the original extraction-provider trade-offs.
