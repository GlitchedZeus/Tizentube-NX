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

### Networking foundation

- [x] Transport-neutral HTTP request/response/client contract
- [x] Search/Browse/Home InnerTube request builders
- [x] Dynamic guest session bootstrap request (`sw.js_data`)
- [x] Strict-TLS Switch libcurl transport
- [x] Host + devkitA64 CI for the networking foundation
- [ ] Pin trusted CA bundle in RomFS and document refresh procedure
- [ ] Parse bootstrap response into a guest session
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
- [ ] 720p handheld / 1080p docked defaults
- [ ] Quality selector
- [ ] Captions
- [ ] Alternate audio tracks
- [ ] Playback speed
- [ ] Seek/resume

## M4 — Easy account login

- [ ] Validate compliant TV/device-code authorization strategy
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
- [ ] Release packaging
