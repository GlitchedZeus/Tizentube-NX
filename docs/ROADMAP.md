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
- [x] Real-Switch guest bootstrap accepted: `Guest ready`
- [x] Borrow Borealis-owned socket environment without tearing it down
- [x] Keep SSL lifetime/ref-count ownership separate from BSD socket ownership

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

### Search execution / privacy / UI activation

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
- [x] Enable explicit live Search POST in Switch UI
- [x] Reuse the memory-only accepted guest session for Search
- [x] Keep live Search networking off the Borealis/UI thread
- [x] Reject stale async completions by SearchModel generation
- [x] Render normalized Video / Channel / Playlist results as text-only Borealis entries
- [x] Controller-focusable result selection seam with clean canonical URL/ID detail
- [x] Explicit Load more continuation flow on Switch
- [x] Continuation retry retains existing first-page results
- [x] Keep remote thumbnail fetching disabled pending separate host review
- [x] Real-Switch startup-recovery shell accepted after pre-first-frame crash
- [x] Re-enable first-page Search on the accepted plain `ScrollingFrame` ownership model
- [x] Keep `Load more` UI disabled for the first Search-reactivation hardware gate
- [x] Real-hardware first-page live Search acceptance
- [x] Attempt dedicated A-open/B-back result-detail activity
- [x] Hardware-reject the dedicated result-detail activity after black-screen/pop-transition failure
- [x] Replace it with an inline selected-result detail block on the stable Search page
- [x] Hardware-observe inline detail rendering at `36015cb...` but reject the overall UI checkpoint after blank-page/sidebar-focus regression
- [x] Remove destructive selection-time result-tree rebuilding and forced result refocus
- [x] Return B to the active SidebarItem instead of the Sidebar container/default Home item
- [x] Real-hardware inline-detail + full sidebar traversal acceptance at `d6910a232732b2bd9169abb11dfdf320cf35a34b`
- [x] Re-enable explicit continuation / `Load more` with append-only result Views at `534f576a77aeb8f7226b2f3a3549be23c621e06a`
- [x] Physically verify continuation fetch/append at `959a46e7c1d5de1743cdf3b11a48b1b227ab0b4e`
- [x] Hardware-reject `959a46e...` focus UX after selector loss and Up-from-Load-more jump to result 1
- [x] Prove pinned Borealis nested-Box/default-focus + ScrollingFrame recenter behavior behind that regression
- [x] Implement focus fix + A-again detail toggle at `d432ef8c6eee8e1b77ecd8a96b78832c43bb8a5b`
- [x] Real-hardware continuation / `Load more` focus/UI acceptance at `428497c90be6e768c607e45c68171827d74f0faf`

### Future feature seams — not active in M2

- [x] Keep normalized data/presentation boundaries independent of future player/metadata services
- [x] Preserve hard no-Shorts invariant regardless of reference implementation behavior
- [x] Reserve SponsorBlock as a core M3 player subsystem rather than a post-playback extra
- [ ] Alternative/DeArrow-style title/thumbnail adapter
- [ ] Return YouTube Dislike adapter
- [ ] Playback-quality preferences
- [ ] Original-audio preference
- [ ] Player cleanup controls
- [ ] Review/allowlist every third-party destination before DNS

### Remaining guest surfaces

- [x] Implement scoped live guest Home first-page request/parser/model/text UI
- [x] Real-hardware Home request path + valid `feedNudgeRenderer` guest empty-state acceptance at `7b4993f7871ae71cb8ca44e30e51f876f8133cc2`
- [ ] Real-hardware normal Home Video/Channel/Playlist rendering when YouTube actually provides normal guest results
- [ ] Home continuation after normal-content Home is physically observable/accepted
- [ ] Channel pages
- [ ] Playlist pages
- [ ] Real-hardware guest browsing acceptance

## M3 — Playback + ad-free viewing

Playback and the ad-free experience are one milestone. **A player that deliberately shows YouTube ads is not considered finished.** See `docs/PRODUCT_PHILOSOPHY.md`.

### Media playback

- [ ] Video metadata/stream resolver
- [ ] mpv + FFmpeg Switch pipeline
- [ ] Apply outbound host policy to every media/CDN destination before DNS
- [ ] 720p handheld / 1080p docked defaults
- [ ] Quality selector
- [ ] Captions / alternate audio / speed / seek / resume

### YouTube ad suppression — release-blocking for playback

- [ ] No pre-roll YouTube ads
- [ ] No mid-roll YouTube ads
- [ ] No post-roll YouTube ads
- [ ] No ad/promoted/shopping/Premium surfaces in player/browse paths
- [ ] No known fallback that re-enables ads merely to make playback succeed
- [ ] Host fixtures/tests for ad-bearing player/metadata structures where practical
- [ ] Real-hardware playback acceptance explicitly verifies no YouTube-served ads

### SponsorBlock — core player subsystem

- [ ] SponsorBlock service adapter behind the normal reviewed outbound policy
- [ ] Controller-friendly SponsorBlock settings page
- [ ] Global SponsorBlock enable/disable
- [ ] Per-category behavior: Auto-skip / Ask or show Skip button / Do not skip
- [ ] Use SponsorBlock's current category semantics at implementation time instead of freezing an outdated category list
- [ ] In-player unobtrusive skip feedback suitable for handheld and docked modes
- [ ] Manual seeking remains sane around skipped segments
- [ ] Cache/retry behavior that does not block basic player controls
- [ ] Real-hardware SponsorBlock acceptance alongside playback acceptance

## M4 — Local TizenTube profiles (v1)

- [ ] Create a local profile directly on Switch
- [ ] Display name + avatar
- [ ] Local channel follows/subscriptions
- [ ] Local playlists
- [ ] Local Watch Later
- [ ] Local likes/favorites
- [ ] Watch history + resume positions
- [ ] Per-profile settings/preferences
- [ ] Optional local search history
- [ ] Multiple local TizenTube profiles
- [ ] Optional Switch-user-to-TizenTube-profile mapping
- [ ] Profile backup/export/import appropriate for local data
- [ ] No Google/YouTube account authentication required for v1
- [ ] Never accept/store Google passwords, browser cookies, exported cookie files or equivalent browser/account credentials

See `docs/PROFILES_AND_IMPORT.md` for the profile/security contract.

## M5 — Morphe-inspired enhancements and metadata

Morphe is a feature-discovery/research source for this milestone; TizenTube and YouTube remain the main UI/settings references.

- [ ] DeArrow/alternative titles/thumbnails
- [ ] Return YouTube Dislike
- [ ] Clean share actions from normalized identifiers
- [ ] Additional player cleanup / quality-of-life ideas selected from Morphe review
- [ ] Third-party API destinations reviewed through outbound policy
- [ ] Additional non-Shorts feed filters

## M6 — Daily-driver polish / v1 release

- [ ] Crash recovery / caching / retry-backoff
- [ ] Dock/undock handling
- [ ] Applet vs title-takeover testing
- [ ] Release audit: playback does not knowingly fall back to YouTube-served ads
- [ ] Release audit: no network path bypasses outbound policy
- [ ] Release audit: v1 contains no Google OAuth/account linking implementation
- [ ] Release audit: no Google passwords/browser cookies/exported cookie files accepted anywhere
- [ ] Release packaging

## M7 — Post-v1 optional YouTube linking and migration

Local TizenTube profiles remain canonical. Post-v1 linking is optional and must not be required to use the app.

### Official device OAuth linking

- [ ] Switch displays QR code plus human-readable device-code fallback
- [ ] Phone opens Google's official sign-in/consent UI
- [ ] Never collect Google passwords, passkeys, 2FA recovery codes or browser cookies
- [ ] Request the narrowest practical YouTube scope; prefer read-only access
- [ ] Initial sync direction is `YouTube -> TizenTube profile`
- [ ] Short-lived access token kept memory-only where practical
- [ ] Optional `Keep me connected` mode using persistent refresh authorization
- [ ] Dedicated secure-storage review before any refresh token is persisted
- [ ] Never store persistent authorization as obvious plaintext on SD
- [ ] `Reconnect YouTube` when authorization expires/revokes/invalidates
- [ ] Local TizenTube profile continues working while disconnected
- [ ] Clear `Unlink YouTube` action removes local authorization and attempts provider revocation when practical
- [ ] Unlinking does not delete local profile data
- [ ] Token/authorization values fully redacted from logs, UI, exports and crash diagnostics
- [ ] All auth/API destinations reviewed through outbound policy before DNS

### Library import/sync

- [ ] Import/sync subscriptions where supported
- [ ] Import/sync playlists where supported
- [ ] Import/sync liked/favorite library data where supported
- [ ] Import approved account/channel metadata needed for migration
- [ ] Duplicate-safe merge into existing local profile
- [ ] Preserve local-only follows/playlists/favorites/history
- [ ] Explicit user control over what is imported/synced

### Optional public/no-auth fallback

- [ ] Public YouTube profile/channel metadata import
- [ ] Public subscriptions import when the source profile exposes them
- [ ] Public playlist import
- [ ] User-supplied unlisted playlist import
- [ ] Treat unlisted playlist URLs as secrets and redact them from diagnostics

See `docs/PROFILES_AND_IMPORT.md` and `docs/OAUTH_ACCOUNT_LINKING.md` for the full post-v1 design.

## Current gate

**Guest bootstrap, startup recovery, the complete Search slice, inline detail, sidebar traversal, and explicit Search continuation are physically accepted through `428497c90be6e768c607e45c68171827d74f0faf`. The current M2 gate is the first explicit live guest Home page. Preserve the accepted Search implementation, exact `www.youtube.com:443` allowlist, hard Shorts/ad/promoted/shopping firewall, remote-thumbnail block, and post-v1 OAuth documentation-only boundary.**


## Current gate — Channel pages

The physical Home diagnostic gate is closed: `7b4993f7871ae71cb8ca44e30e51f876f8133cc2` proved the `FEwhat_to_watch` request path and a valid terminal `feedNudgeRenderer` guest empty state. Normal Home content was not provided by YouTube, so normal-result rendering remains host-validated rather than hardware-accepted. Do not fake Trending as Home. Preserve the exact `www.youtube.com:443` allowlist, accepted Search baseline, typed Home empty state, remote-thumbnail block, and hard Shorts/ad/shopping firewall while implementing Channel pages next.
