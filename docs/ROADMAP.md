# Roadmap

## M0 — Foundation

- [x] Repository structure
- [x] Host build + test harness
- [x] Canonical clean YouTube share URL generator
- [x] Hard Shorts content filter invariant
- [x] Minimal libnx boot skeleton
- [x] Build the first `.nro` in a devkitPro environment and test on Atmosphere hardware

## M1 — Switch application shell

- [x] Add Borealis
- [x] Home/Search/Subscriptions/Library/Settings navigation shell
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
- [x] Disable autonomous curl redirects
- [x] Host tests for Nintendo roots, deep subdomains and deceptive URL forms
- [x] Require every native HTTP redirect target to pass policy before a second DNS lookup
- [ ] Require every future network subsystem (media/images/auth/updater/etc.) to use the same policy

### Networking foundation

- [x] Transport-neutral HTTP request/response/client contract
- [x] Search/Browse/Home InnerTube request builders
- [x] Dynamic guest session bootstrap request (`sw.js_data`)
- [x] Prototype strict-verification Switch libcurl transport
- [x] Identify current devkitPro switch-curl cert-info crash risk and block it from becoming default live transport
- [x] Host + devkitA64 CI for the networking foundation
- [x] Parse bootstrap response into a guest session
- [x] Strict bounded HTTP/1.1 request/response codec with host tests
- [x] Direct libnx SSL-service HTTP/1.1 transport
- [x] GET + POST serialization with bounded request/response bodies
- [x] Configure peer-CA + hostname + certificate-date verification using the Switch SSL service
- [x] Remove switch-curl and its TLS/zlib dependencies from the NRO link path
- [x] DevkitA64 compile/link gate for the native libnx transport
- [ ] First live verified HTTPS request on Switch
- [ ] Move live networking off the Borealis UI thread

### Guest data and presentation

- [x] Renderer-neutral Home/Search/Channel/Playlist data model
- [x] Opaque continuation/pagination model
- [x] Renderer firewall for Shorts/reels, promoted/ad, shopping and unsupported renderers
- [x] Tests proving disguised Shorts/promoted renderers cannot become visible records
- [ ] Parse live Search results
- [ ] Parse live Home/browse results
- [ ] Channel pages
- [ ] Playlist pages
- [ ] Live continuation paging
- [ ] Render sanitized guest results as Borealis cards
- [ ] Real-hardware guest browsing acceptance

## M3 — Playback

- [ ] Video metadata/stream resolver
- [ ] mpv + FFmpeg Switch pipeline
- [ ] Apply outbound host policy to every media/CDN destination before DNS
- [ ] 720p handheld / 1080p docked defaults
- [ ] Quality selector
- [ ] Captions
- [ ] Alternate audio tracks
- [ ] Playback speed
- [ ] Seek/resume

## M4 — Easy account login

- [ ] Validate compliant TV/device-code authorization strategy
- [ ] Apply outbound host policy to every auth destination before DNS
- [ ] QR/code sign-in view
- [ ] Token/session persistence
- [ ] Refresh/recovery/sign-out
- [ ] Profile identity
- [ ] Personalized Home
- [ ] Subscriptions
- [ ] Likes/playlists/history features supported by the chosen account backend

## M5 — TizenTube/ReVanced features

- [ ] SponsorBlock categories + auto-skip/skip-button modes
- [ ] DeArrow titles
- [ ] DeArrow thumbnails
- [ ] Apply outbound host policy to all third-party API destinations before DNS
- [ ] Ad/promotion suppression hardening
- [ ] Hide end-screen clutter
- [ ] Optional additional feed filters

## M6 — Daily-driver polish

- [ ] Crash recovery
- [ ] Caching
- [ ] Network retry/backoff
- [ ] Dock/undock handling
- [ ] Applet vs title-takeover testing
- [ ] Multiple Switch profiles/account mapping (research + implementation)
- [ ] Release audit: no network code path can bypass the outbound policy
- [ ] Release packaging
