# TizenTube NX

[![Host tests](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/host-tests.yml/badge.svg?branch=feature%2Fm2-guest-browsing)](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/host-tests.yml)
[![Switch build](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/switch-build.yml/badge.svg?branch=feature%2Fm2-guest-browsing)](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/switch-build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

**TizenTube NX** is a native, controller-first YouTube client for Nintendo Switch homebrew.

The target is simple: make it feel like **TizenTube/YouTube was intentionally brought to Nintendo Switch** — familiar navigation and settings, but without Shorts, YouTube ads, tracking-heavy links or unnecessary engagement clutter.

> **Status: pre-alpha / active development.** Guest Search and live normal-video H.264/AAC playback have been exercised on real Switch hardware. The experimental M4 rolling-cache and long-video continuation path remains isolated, with native build, hardware and ad-free acceptance still open.

## Product philosophy

A few rules define the project more strongly than individual features:

- **Low-RAM playback with a disposable SD cache.** Stream progressively through a rolling ~25 MB SD working cache; do not download whole videos or reserve large video buffers in RAM. The entire installed app, persistent state, and temporary media must remain within **100,000,000 bytes on SD**, and the media cache is deleted after playback. See [the accepted M4 SD-streaming requirements](docs/M4_SD_STREAMING_REQUIREMENTS.md).
- **Playback and ad-free viewing are equally important.** A player that deliberately shows YouTube-served pre-roll, mid-roll or post-roll ads is not finished playback.
- **SponsorBlock is part of the core player experience.** It will have Switch-native settings for per-category behavior such as auto-skip, ask/show a Skip button, or do not skip.
- **No Shorts.** Shorts/reels are filtered at the data boundary and are not a user-facing feature.
- **No promoted/shopping/Premium clutter.** Ads and promotional surfaces do not become acceptable because YouTube sends them alongside normal content.
- **Clean links only.** A video shares as `https://www.youtube.com/watch?v=VIDEO_ID`, without tracking junk.
- **Switch-first UX.** Controller navigation comes first, with touch where it genuinely helps.
- **No Google accounts or linking — ever.** TizenTube NX runs without Google/YouTube sign-in, OAuth, cookies, credentials, sync or account profiles. Watch history, resume, Watch Later, favorites, playlists and followed channels are stored **locally on the Switch**, with a small bounded metadata footprint. No login or profile creation is required to use the library.
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

The separate M3/M4 engineering branch is [`feature/m3-playback-foundation-20261011`](https://github.com/GlitchedZeus/Tizentube-NX/tree/feature/m3-playback-foundation-20261011) (draft PR [#8](https://github.com/GlitchedZeus/Tizentube-NX/pull/8)). The owner has confirmed real YouTube picture/audio playback including a ten-minute video on physical Switch. This does **not** accept broad compatibility, uninterrupted rolling playback or a finished player.

An **isolated offline libmpv video probe** is now implemented on that M3 branch. It builds as `tizentube_nx_m3_probe.nro` **only** with `TTNX_M3_OFFLINE_PROBE=1`, using the independent [M3 offline probe CI workflow](https://github.com/GlitchedZeus/Tizentube-NX/actions/workflows/m3-offline-probe.yml). This is a local synthetic-MP4 experiment, not an enabled player in the normal NRO. **The separate offline player has now passed physical Switch H.264/AAC playback and basic controller tests** (pause/resume, both seek directions, stop and reload/replay) at exact tested NRO source `2acc3b75`. The Switch reported `nvtegra-copy` decoding. The owner subsequently verified the louder audio fixture and sleep/wake behavior on physical Switch as well. Full YouTube streaming, ad-free integration, extended A/V sync and dock/undock reliability remain unverified. See [`docs/M3_HARDWARE_CHECKPOINT_2026-10-11.md`](docs/M3_HARDWARE_CHECKPOINT_2026-10-11.md) and [`docs/M3_OFFLINE_PROBE.md`](docs/M3_OFFLINE_PROBE.md).

**M3 full-media engineering is now underway**, beyond the video probe: a native guest YouTube player-metadata request/parser, reviewed Googlevideo CDN byte-range reader, cancellable staged SD cache, opt-in native Horizon SSL media transport, and a progressive H.264/AAC source-to-cache pipeline with host tests. These are **not** yet connected to the production UI or approved for live CDN use on hardware. The next substantial NRO milestone is one integrated normal-YouTube-video playback candidate rather than minor probe rerolls; implementation and security gates are tracked in [M3 media integration](docs/M3_MEDIA_INTEGRATION_2026-10-11.md).


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

The Search pagination focus/scroll gate passed hardware testing at `428497c90be6e768c607e45c68171827d74f0faf`. Guest Home request handling has been physically observed, while Channel first-page support is host-validated and still needs its own device gate.

For exact accepted/rejected hardware checkpoints, see the active-branch [`HARDWARE_GATES.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/HARDWARE_GATES.md).


### M4 isolated streaming hardware lab (not a release)

Draft PR #8 now includes an explicitly opted-in `TTNX_M4_NATIVE_TEST_UI=1` route for an independently validated **normal Search result** to enter a native HLS source probe, strict source/format/CDN admission, a 24 MiB-minus-metadata SD ring and the libmpv custom-stream callback. It uses one media worker and waits for a completely validated MPEG-TS prefix before handing off to the player UI. The default M3 Play button and normal NRO do not enable this route.

The underlying decoder-epoch continuation, ~15s playback-aware prefetch, Stop cancellation, reader ownership, timestamp normalization, 10m+ synthetic epoch regressions and fail-closed HLS admission are implemented. They are **not yet evidence of smooth live long-video playback on Switch**. The opt-in lab NRO is compiled in a separate CI target and is not distributed automatically; CI can remain queued. See [M4 native Search lab and acceptance checklist](docs/M4_NATIVE_SEARCH_LAB.md).

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
- Google/YouTube account authentication is deliberately **out of scope for all releases**.

The app does not rely on 90DNS for these protections.

More detail: [`NETWORK_SAFETY.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/NETWORK_SAFETY.md) and [`NATIVE_NETWORK_LIFETIME.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/NATIVE_NETWORK_LIFETIME.md).

## Roadmap

| Milestone | Status | Goal |
| --- | --- | --- |
| **M0 — Foundation** | ✅ Complete | Repository, host tests, clean URL handling, no-Shorts invariant, first NRO |
| **M1 — Switch shell** | ✅ Complete | Borealis UI, controller/touch navigation, storage, real-hardware shell acceptance |
| **M2 — Guest browsing** | 🚧 In progress | Safe guest networking, Search, pagination, Home/channel/playlist guest surfaces |
| **M3 — Playback + ad-free viewing** | 🚧 Real-video hardware baseline; feature-complete player pending | Media pipeline, no YouTube-served ads, SponsorBlock + settings, quality/captions/audio/seek/resume |
| **M4 — On-device Library** | ⏳ Planned | Local watch history/resume, Watch Later, favorites, followed channels and playlists; small metadata-only storage |
| **M5 — Morphe-inspired enhancements** | ⏳ Planned | DeArrow-style metadata, Return YouTube Dislike, player cleanup and selected quality-of-life ideas |
| **M6 — Daily-driver polish / v1** | ⏳ Planned | Stability, caching/retry, dock/undock, applet/title-takeover testing, release packaging |
| **M7 — Local Library portability** | ⏳ Optional | Compact backup/export/import of on-device metadata; no Google sign-in or account synchronization |

The detailed checklist lives in the active-branch [`docs/ROADMAP.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ROADMAP.md).

### Current M2 focus

Guest bootstrap, startup recovery, first-page live Search, inline result details and full sidebar traversal are physically accepted on a real Switch.

`Load more` now has physically accepted focus/navigation. The Home guest endpoint has also been exercised on real hardware; for that guest it returned an empty normal-content state. Channel first-page parsing/UI is host-tested but requires its own Switch acceptance, and playlist browsing remains outstanding.

An isolated **M3 playback foundation** is being developed in draft PR [#8](https://github.com/GlitchedZeus/Tizentube-NX/pull/8), based on M2, without enabling media networking or video playback. See [M3 foundation details](docs/M3_PLAYBACK_FOUNDATION.md).

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

- generally compatible, uninterrupted long-video playback via the bounded M4 SD ring;
- fully verified, release-ready YouTube ad-free playback and SponsorBlock UI;
- SponsorBlock integration/settings;
- remote thumbnails;
- live Home/channel/playlist browsing pages;
- on-device watch history, resume, Watch Later, favorites and local playlists;
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

- [Real Switch UI capture layout review](docs/SWITCH_CAPTURE_LAYOUT_REVIEW.md) — 37 user-supplied 1280×720 screenshots analyzed privately: sidebar, video rails, player overlay, quality/audio panels, Library, channel/following views and test-backed original layout geometry.
- [YouTube Switch NSP forensic/layout reference](docs/SWITCH_YOUTUBE_NSP_UI_REFERENCE.md) — encrypted NSP inspection, evidence limits and native TizenTube NX implementation boundaries; no proprietary assets copied.

Other historical development documentation originally lives on `feature/m2-guest-browsing`:

- [`PRODUCT_PHILOSOPHY.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/PRODUCT_PHILOSOPHY.md) — product/reference hierarchy and ad-free playback contract
- [`PROJECT_STATUS.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/PROJECT_STATUS.md) — current implementation status and active gate
- [`HARDWARE_GATES.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/HARDWARE_GATES.md) — what has actually passed or failed on a physical Switch
- [`ROADMAP.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ROADMAP.md) — full milestone checklist
- [`SCOPE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/SCOPE.md) — project scope and product boundaries
- [`ARCHITECTURE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/ARCHITECTURE.md) — code architecture
- [`NETWORK_SAFETY.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/NETWORK_SAFETY.md) — outbound-network policy
- [`PROFILES_AND_IMPORT.md`](docs/PROFILES_AND_IMPORT.md) — bounded on-device library, history and optional local-only profile settings
- [`OAUTH_ACCOUNT_LINKING.md`](docs/OAUTH_ACCOUNT_LINKING.md) — archived and explicitly rejected OAuth proposal (no Google account linking)
- [`MORPHE_REFERENCE.md`](https://github.com/GlitchedZeus/Tizentube-NX/blob/feature/m2-guest-browsing/docs/MORPHE_REFERENCE.md) — Morphe's feature-discovery/research role

## Contributing / development

This is currently an actively changing homebrew project rather than a stable public release. Small, focused changes are preferred, especially around networking, parser behavior, controller focus and hardware-tested UI paths.

When changing network behavior, do not widen the allowlist casually. New media, SponsorBlock or metadata hosts must receive explicit policy review rather than being added just because an upstream response references them.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).

TizenTube NX is an independent homebrew project and is not affiliated with Google, YouTube, TizenTube, SponsorBlock, NewPipe, Morphe or Nintendo.
