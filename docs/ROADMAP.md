# Roadmap

## M0 — Foundation

- [x] Repository structure
- [x] Host build + test harness
- [x] Canonical clean YouTube share URL generator
- [x] Hard Shorts content filter invariant
- [x] Minimal libnx boot skeleton
- [x] First `.nro` build + Atmosphere hardware test

## M1 — Switch application shell

- [x] Borealis shell
- [x] Home/Search/Subscriptions/Library/Settings navigation
- [x] Controller + touch interaction contract
- [x] SD-card settings/log paths
- [x] App icon/resources
- [x] Device acceptance on real Atmosphere hardware

## M2 — YouTube guest browsing

### Console/network safety

- [x] Default-deny outbound host policy
- [x] Explicit Nintendo-domain hard deny independent of 90DNS
- [x] Enforce host policy before DNS/connect
- [x] Block HTTP, alternate ports, IP literals and userinfo authority tricks
- [x] Revalidate every redirect before a second DNS lookup
- [x] Keep exact live allowlist at `www.youtube.com:443`
- [x] Exclude `switch-curl` from the NRO
- [ ] Require every future media/image/auth/updater subsystem to use the same policy

### Networking foundation

- [x] Transport-neutral HTTP request/response/client contract
- [x] Search/Browse/Home request builders
- [x] Dynamic `sw.js_data` guest bootstrap request/parser
- [x] Strict bounded HTTP/1.1 codec
- [x] Direct libnx BSD socket + Horizon SSL transport
- [x] Peer-CA + hostname + date verification
- [x] User-triggered off-UI-thread guest bootstrap probe
- [ ] First verified real-Switch guest bootstrap result

### Search parsing and normalized data

- [x] Scoped Search primary/continuation boundary
- [x] Legacy `videoRenderer`
- [x] Modern `lockupViewModel`
- [x] Legacy + modern channel results
- [x] Legacy + modern playlist results
- [x] Legacy + modern continuation items
- [x] Renderer firewall for Shorts/reels, ads/promoted, shopping and unsupported renderers
- [x] Opaque blocked subtrees
- [x] Renderer-agnostic Video / Channel / Playlist result model
- [x] Stable type-qualified result identity
- [x] Deterministic post-firewall dedupe
- [x] Bounded optional video metadata normalization
- [x] Bounded optional channel metadata normalization
- [x] Bounded optional playlist metadata normalization
- [x] Conservative exact count parsing with localized/abbreviated text fallback
- [x] Live / upcoming / scheduled-time normalized state
- [x] Bounded thumbnail candidate model with dimensions
- [x] Deterministic best-first thumbnail preference and output cap
- [x] Hostile/odd Search fixture corpus
- [x] Deterministic mutation/property-style parser corpus
- [x] Numeric-overflow / malformed-grouping / conflicting-optional-value tests
- [x] Guard low-level unscoped parser as an internal API

### URLs / local routing

- [x] Strict normal video-ID validation
- [x] Clean canonical video URL: `https://www.youtube.com/watch?v=VIDEO_ID`
- [x] Canonical stable channel-ID URL helper
- [x] Canonical playlist URL helper
- [x] Strip tracking/query contamination during canonicalization
- [x] Reject duplicate/conflicting `v` values and malformed/oversized inputs
- [x] Reject Shorts/reel URLs rather than converting them into normal videos

### Search execution / privacy / offline UI preparation

- [x] Stateless transport-neutral Search executor
- [x] First-page and continuation fake-HTTP tests
- [x] Continuation requests do not resend query
- [x] Query-owned pagination state machine
- [x] Repeated-token loop prevention
- [x] HTTP/parse failure leaves continuation retryable
- [x] New query invalidates old continuation state
- [x] Bound/minimize reviewed WEB/v1 request contract
- [x] Tests proving analytics/ad/tracking-style JSON fields are absent
- [x] Sensitive diagnostic replacement + length bounds
- [x] UI-safe Search error taxonomy separate from technical diagnostics
- [x] Offline Search model with loading/ready/empty/error/loading-more states
- [x] End-of-results and continuation-availability state
- [x] Retryable continuation/network failure state
- [x] Stable selection identity + stale-generation rejection
- [x] Pure duration/live/upcoming/view/upload-age/count display helpers
- [ ] Enable live Search POST in Switch UI — **blocked pending real-Switch bootstrap result**
- [ ] Render normalized Search results as Borealis cards
- [ ] Live continuation paging on Switch

### Future feature seams — not active in M2

- [x] Keep normalized data/presentation boundaries independent of future player/metadata services
- [x] Preserve hard no-Shorts invariant regardless of reference implementation behavior
- [ ] SponsorBlock adapter
- [ ] Alternative/DeArrow-style title/thumbnail adapter
- [ ] Return YouTube Dislike adapter
- [ ] Playback-quality preferences
- [ ] Original-audio preference
- [ ] Player cleanup controls
- [ ] Review/allowlist every third-party destination before DNS

### Remaining guest surfaces

- [ ] Parse/present live Home browse results
- [ ] Channel pages
- [ ] Playlist pages
- [ ] Real-hardware guest browsing acceptance

## M3 — Playback

- [ ] Video metadata/stream resolver
- [ ] mpv + FFmpeg Switch pipeline
- [ ] Apply outbound host policy to every media/CDN destination before DNS
- [ ] 720p handheld / 1080p docked defaults
- [ ] Quality selector
- [ ] Captions / alternate audio / speed / seek / resume

## M4 — Account login

- [ ] Validate compliant TV/device-code authorization strategy
- [ ] Apply outbound host policy to every auth destination before DNS
- [ ] QR/code sign-in
- [ ] Token/session persistence + refresh/recovery/sign-out
- [ ] Subscriptions/likes/playlists/history as supported by chosen backend

## M5 — TizenTube/ReVanced feature integrations

- [ ] SponsorBlock
- [ ] DeArrow/alternative titles/thumbnails
- [ ] Return YouTube Dislike
- [ ] Clean share actions from normalized identifiers
- [ ] Third-party API destinations reviewed through outbound policy
- [ ] Additional non-Shorts feed filters

## M6 — Daily-driver polish

- [ ] Crash recovery / caching / retry-backoff
- [ ] Dock/undock handling
- [ ] Applet vs title-takeover testing
- [ ] Multiple Switch profiles/account mapping
- [ ] Release audit: no network path bypasses outbound policy
- [ ] Release packaging

## Current gate

**Live Search remains disabled pending the real-Switch `Test YouTube guest connection` result.**
