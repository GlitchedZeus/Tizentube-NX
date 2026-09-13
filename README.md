# TizenTube NX

[![Host tests](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/host-tests.yml/badge.svg?branch=feature%2Fm2-guest-browsing)](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/host-tests.yml)
[![Switch build](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/switch-build.yml/badge.svg?branch=feature%2Fm2-guest-browsing)](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/switch-build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

**TizenTube NX** is a native, controller-first YouTube client for Nintendo Switch homebrew.

The target is simple: make it feel like **TizenTube/YouTube was intentionally brought to Nintendo Switch** — familiar navigation and settings, but without Shorts, YouTube ads, tracking-heavy links or unnecessary engagement clutter.

> **Status: pre-alpha / active development.** Guest browsing and Search are being brought up first. Playback comes later, and playback is not considered complete unless the ad-free experience is complete with it.

## Product philosophy

A few rules define the project more strongly than individual features:

- **Playback and ad-free viewing are equally important.** A player that deliberately shows YouTube-served pre-roll, mid-roll or post-roll ads is not finished playback.
- **SponsorBlock is part of the core player experience.** It will have Switch-native settings for per-category behavior such as auto-skip, ask/show a Skip button, or do not skip.
- **No Shorts.** Shorts/reels are filtered at the data boundary and are not a user-facing feature.
- **No promoted/shopping/Premium clutter.** Ads and promotional surfaces do not become acceptable because YouTube sends them alongside normal content.
- **Clean links only.** A video shares as `https://www.youtube.com/watch?v=VIDEO_ID`, without tracking junk.
- **Switch-first UX.** Controller navigation comes first, with touch where it genuinely helps.
- **Local-first profiles for v1.** TizenTube profiles, follows, playlists, Watch Later, favorites and history are planned without requiring a Google account.
- **No credential scraping.** The project will not ask for Google passwords, browser cookies or exported cookie files.

The full contract is in [`docs/PRODUCT_PHILOSOPHY.md`](docs/PRODUCT_PHILOSOPHY.md).

### Reference roles

TizenTube NX uses different projects for different kinds of guidance:

- **YouTube** — familiar information architecture and concepts users already know.
- **TizenTube** — the primary UI/settings and ad-free viewing-behavior guide.
- **SponsorBlock** — the primary reference for community-defined sponsor/segment skipping behavior and category settings.
- **Morphe** — feature discovery and development research for future quality-of-life ideas.
- **NewPipe** — a lightweight/privacy-conscious independent-client and technical/product reference.

**ReVanced is not a current TizenTube NX product-philosophy/design reference.**

## What works today

The active development branch is [`feature/m2-guest-browsing`](https://github.com/GlitchedZeus/Tizentube-NX/tree/feature/m2-guest-browsing) with draft PR [#6](https://github.com/GlitchedZeus/Tizentube-NX/pull/6).

On real Atmosphère hardware, the project has already passed these gates:

- native Borealis application shell with **Home, Search, Subscriptions, Library and Settings**;
- Joy-Con / Pro Controller navigation and Switch software keyboard input;
- native libnx networking with verified HTTPS and a memory-only YouTube guest session;
- live guest **Search** against YouTube;
- normalized Video / Channel / Playlist result parsing;
- text-only result browsing with metadata such as uploader, duration, views and upload age when available;
- inline result details with stable IDs and clean canonical URLs;
- full sidebar traversal after Search without the earlier blank-page/focus regression;
- explicit **Load more** continuation that has successfully fetched and appended more results on hardware.

The continuation data path works. The current M2 gate is the controller focus/scroll behavior after pagination; the latest focus fix is awaiting physical acceptance.

For exact accepted/rejected hardware checkpoints, see the active-branch [`HARDWARE_GATES.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/HARDWARE_GATES.md).

## Network & privacy model

TizenTube NX deliberately starts with a very small network surface.

Current M2 policy:

- exact outbound allowlist: **`www.youtube.com:443` only**;
- Nintendo network domains are **hard-denied before DNS**;
- HTTPS only, port 443 only;
- redirects are revalidated before another DNS lookup;
- IP literals, URL userinfo and malformed authorities are rejected;
- TLS peer CA, hostname and certificate dates are verified;
- `switch-curl` is not linked into the NRO;
- remote thumbnail/CDN hosts are still disabled until reviewed separately;
- Google/YouTube account authentication is not active in M2.

The app does not rely on 90DNS for these protections.

More detail: [`NETWORK_SAFETY.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/NETWORK_SAFETY.md) and [`NATIVE_NETWORK_LIFETIME.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/NATIVE_NETWORK_LIFETIME.md).

## Roadmap

| Milestone | Status | Goal |
| --- | --- | --- |
| **M0 — Foundation** | ✅ Complete | Repository, host tests, clean URL handling, no-Shorts invariant, first NRO |
| **M1 — Switch shell** | ✅ Complete | Borealis UI, controller/touch navigation, storage, real-hardware shell acceptance |
| **M2 — Guest browsing** | 🚧 In progress | Safe guest networking, Search, pagination, Home/channel/playlist guest surfaces |
| **M3 — Playback + ad-free viewing** | ⏳ Planned | Media pipeline, no YouTube-served ads, SponsorBlock + settings, quality/captions/audio/seek/resume |
| **M4 — Local profiles** | ⏳ Planned | Local subscriptions/follows, playlists, Watch Later, likes/favorites, history, multiple profiles |
| **M5 — Morphe-inspired enhancements** | ⏳ Planned | DeArrow-style metadata, Return YouTube Dislike, player cleanup and selected quality-of-life ideas |
| **M6 — Daily-driver polish / v1** | ⏳ Planned | Stability, caching/retry, dock/undock, applet/title-takeover testing, release packaging |
| **M7 — Optional post-v1 YouTube linking** | ⏳ Planned | Official limited-input/device OAuth and controlled import/sync into local profiles |

The detailed checklist lives in the active-branch [`docs/ROADMAP.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ROADMAP.md).

### Current M2 focus

Guest bootstrap, startup recovery, first-page live Search, inline result details and full sidebar traversal are physically accepted on a real Switch.

`Load more` has also proven that continuation requests can fetch and append additional normalized results. The remaining pagination gate is UI-focused: keep the selector visible, preserve natural Up/Down movement around the last result / `Load more` boundary, and keep the accepted Search/sidebar lifecycle intact.

After that, M2 continues with guest Home browsing, channel pages and playlist pages before moving into M3 playback + ad-free viewing.

## Planned player philosophy

M3 is deliberately broader than “make a video play.” The player is accepted only as a complete viewing experience:

1. normal video starts and plays reliably;
2. no YouTube-served ad is deliberately shown;
3. seek/pause/resume work;
4. quality/audio/captions controls work where supported;
5. SponsorBlock honors the user's category settings;
6. controller focus/overlays remain stable;
7. no Shorts route or ad-bearing fallback sneaks around the product rules;
8. every media/SponsorBlock host receives explicit outbound-policy review before DNS.

SponsorBlock and YouTube-ad suppression are separate layers: SponsorBlock skips community-marked sections inside the creator's video, while TizenTube NX's ad-free contract covers platform-served advertising.

## Not implemented yet

TizenTube NX is still early. In particular, the current builds do **not** yet provide:

- video playback;
- YouTube ad-free playback pipeline;
- SponsorBlock integration/settings;
- remote thumbnails;
- live Home/channel/playlist browsing pages;
- local TizenTube profiles;
- YouTube/Google account linking;
- DeArrow or Return YouTube Dislike integration;
- a polished end-user release installer/update flow.

If you are looking for a finished YouTube replacement today, this repository is not there yet.

## Architecture at a glance

The current Search path is intentionally layered:

```text
Borealis UI
    ↓
SearchModel
    ↓
GuestSearchFlow
    ↓
background worker
    ↓
app-lifetime LibnxHttpClient
    ↓
www.youtube.com
    ↓
scoped parser + Shorts/ad firewall
    ↓
normalized BrowseResult
    ↓
controller-first result UI
```

The UI never parses raw YouTube renderer JSON directly.

See the active-branch [`ARCHITECTURE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ARCHITECTURE.md) and [`PROJECT_STATUS.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/PROJECT_STATUS.md) for deeper implementation notes.

## Building on a PC

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tizentube_nx_host
```

## Building the Switch NRO

Requires devkitPro/devkitA64, libnx, deko3d and the uam shader compiler.

```bash
git submodule update --init --recursive
make -f Makefile.switch -j2
```

GitHub Actions also builds the NRO with the devkitPro toolchain and keeps exact debug symbols for hardware crash investigation.

Runtime preferences and the bounded boot log use:

```text
sdmc:/switch/TizenTube-NX/
```

See [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md) for dependency attribution.

## Useful project docs

The newest development documentation currently lives on `feature/m2-guest-browsing`:

- [`PRODUCT_PHILOSOPHY.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/PRODUCT_PHILOSOPHY.md) — product/reference hierarchy and ad-free playback contract
- [`PROJECT_STATUS.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/PROJECT_STATUS.md) — current implementation status and active gate
- [`HARDWARE_GATES.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/HARDWARE_GATES.md) — what has actually passed or failed on a physical Switch
- [`ROADMAP.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ROADMAP.md) — full milestone checklist
- [`SCOPE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/SCOPE.md) — project scope and product boundaries
- [`ARCHITECTURE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ARCHITECTURE.md) — code architecture
- [`NETWORK_SAFETY.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/NETWORK_SAFETY.md) — outbound-network policy
- [`PROFILES_AND_IMPORT.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/PROFILES_AND_IMPORT.md) — local-first profile direction
- [`OAUTH_ACCOUNT_LINKING.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/OAUTH_ACCOUNT_LINKING.md) — post-v1 optional account-linking design
- [`MORPHE_REFERENCE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/MORPHE_REFERENCE.md) — Morphe's feature-discovery/research role

## Contributing / development

This is currently an actively changing homebrew project rather than a stable public release. Small, focused changes are preferred, especially around networking, parser behavior, controller focus and hardware-tested UI paths.

When changing network behavior, do not widen the allowlist casually. New media, SponsorBlock or metadata hosts must receive explicit policy review rather than being added just because an upstream response references them.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).

TizenTube NX is an independent homebrew project and is not affiliated with Google, YouTube, TizenTube, SponsorBlock, NewPipe, Morphe or Nintendo.
