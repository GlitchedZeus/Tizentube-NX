# TizenTube NX

[![Host tests](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/host-tests.yml/badge.svg?branch=feature%2Fm2-guest-browsing)](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/host-tests.yml)
[![Switch build](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/switch-build.yml/badge.svg?branch=feature%2Fm2-guest-browsing)](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/switch-build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

**TizenTube NX** is a native, controller-first YouTube client for Nintendo Switch homebrew.

The goal is simple: keep the useful parts of YouTube, remove the clutter, make the interface feel at home on a Switch, and build the networking/privacy model conservatively from the start.

The project takes inspiration from **TizenTube**, **ReVanced**, **NewPipe** and **Morphe**, while using its own native Switch architecture and UI.

> **Status: pre-alpha / active development.** TizenTube NX is not a daily-driver YouTube replacement yet. Guest browsing and Search are being brought up first; playback comes later.

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

The continuation data path works, but its **focus/scroll UX is still under hardware review**. The current M2 gate is making pagination navigation feel correct after results are appended.

For exact accepted/rejected hardware checkpoints, see [`docs/HARDWARE_GATES.md`](docs/HARDWARE_GATES.md).

## Project rules

These are not optional preferences; they are part of the design contract.

- **No Shorts.** Shorts/reels are filtered at the data boundary and are not a user-facing feature.
- **No YouTube ads or promoted/shopping renderers.**
- **Clean links only.** A video shares as `https://www.youtube.com/watch?v=VIDEO_ID`, without tracking junk.
- **Switch-first UX.** Controller navigation comes first, with touch used where it actually helps.
- **Local-first profiles for v1.** TizenTube profiles, follows, playlists, Watch Later, favorites and history are planned without requiring a Google account.
- **No credential scraping.** The project will not ask for Google passwords, browser cookies or exported cookie files.
- **Future enhancement support.** SponsorBlock, DeArrow-style metadata, Return YouTube Dislike and other Morphe/ReVanced-style quality-of-life features are planned as separate reviewed integrations.

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

More detail: [`docs/NETWORK_SAFETY.md`](docs/NETWORK_SAFETY.md) and [`docs/NATIVE_NETWORK_LIFETIME.md`](docs/NATIVE_NETWORK_LIFETIME.md).

## Roadmap

| Milestone | Status | Goal |
| --- | --- | --- |
| **M0 — Foundation** | ✅ Complete | Repository, host tests, clean URL handling, no-Shorts invariant, first NRO |
| **M1 — Switch shell** | ✅ Complete | Borealis UI, controller/touch navigation, storage, real-hardware shell acceptance |
| **M2 — Guest browsing** | 🚧 In progress | Safe guest networking, Search, pagination, Home/channel/playlist guest surfaces |
| **M3 — Playback** | ⏳ Planned | Stream resolver, Switch playback pipeline, quality/captions/audio/seek/resume |
| **M4 — Local profiles** | ⏳ Planned | Local subscriptions/follows, playlists, Watch Later, likes/favorites, history, multiple profiles |
| **M5 — Enhancements** | ⏳ Planned | SponsorBlock, DeArrow-style titles/thumbnails, Return YouTube Dislike, player cleanup |
| **M6 — Daily-driver polish / v1** | ⏳ Planned | Stability, caching/retry, dock/undock, applet/title-takeover testing, release packaging |
| **M7 — Optional post-v1 YouTube linking** | ⏳ Planned | Official limited-input/device OAuth and controlled import/sync into local profiles |

The detailed checklist lives in [`docs/ROADMAP.md`](docs/ROADMAP.md).

### Current M2 focus

Guest bootstrap, startup recovery, first-page live Search, inline result details and full sidebar traversal are physically accepted on a real Switch.

`Load more` has also proven that continuation requests can fetch and append additional normalized results. The remaining pagination gate is UI-focused: keep the selector visible, preserve natural Up/Down movement around the last result / `Load more` boundary, and keep the accepted Search/sidebar lifecycle intact.

After that, M2 continues with guest Home browsing, channel pages and playlist pages before moving into playback.

## Not implemented yet

TizenTube NX is still early. In particular, the current builds do **not** yet provide:

- video playback;
- remote thumbnails;
- live Home/channel/playlist browsing pages;
- local TizenTube profiles;
- YouTube/Google account linking;
- SponsorBlock, DeArrow or Return YouTube Dislike integration;
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

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) for the deeper implementation notes.

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

- [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) — current implementation status and active gate
- [`docs/HARDWARE_GATES.md`](docs/HARDWARE_GATES.md) — what has actually passed or failed on a physical Switch
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — full milestone checklist
- [`docs/SCOPE.md`](docs/SCOPE.md) — project scope and product boundaries
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — code architecture
- [`docs/NETWORK_SAFETY.md`](docs/NETWORK_SAFETY.md) — outbound-network policy
- [`docs/PROFILES_AND_IMPORT.md`](docs/PROFILES_AND_IMPORT.md) — local-first profile direction
- [`docs/OAUTH_ACCOUNT_LINKING.md`](docs/OAUTH_ACCOUNT_LINKING.md) — post-v1 optional account-linking design
- [`docs/MORPHE_REFERENCE.md`](docs/MORPHE_REFERENCE.md) — future feature/behavior reference policy

## Contributing / development

This is currently an actively changing homebrew project rather than a stable public release. Small, focused changes are preferred, especially around networking, parser behavior, controller focus and hardware-tested UI paths.

When changing network behavior, do not widen the allowlist casually. New hosts should be treated as an explicit security review, not just added because a YouTube response happens to reference them.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).

TizenTube NX is an independent homebrew project and is not affiliated with Google, YouTube, TizenTube, ReVanced, NewPipe, Morphe or Nintendo.
