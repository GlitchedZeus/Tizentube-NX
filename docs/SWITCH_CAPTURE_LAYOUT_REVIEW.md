# TizenTube NX — 37 real Switch UI capture measurements

**Source:** Two batches of 17 and 20 user-provided screenshots captured at
1280×720 from the patched Nintendo Switch YouTube client, October 12, 2026.

**Privacy rule:** Screenshots include search history, account details and
pairing data. They are private design references ONLY. Do not upload the
screenshots, transcribe account/pairing identifiers, import their art, or
embed their copyrighted thumbnails into TizenTube NX. The native client
implements original layout primitives and controller behavior.

These measurements are approximate screenshot observations, not official
developer specifications. They do not reveal the NSP's decoder,
hardware-acceleration technique, network protocol, cache, or runtime RAM.

## Browsing layout

| Element | Reference | Native TizenTube NX |
|---|---|---|
| Switch canvas | 1280×720, dark background | Scale all geometry to viewport; no browser runtime |
| Collapsed navigation | Icon rail ≈88 px | Original 5 root destinations; no Shorts/account/promos |
| Expanded navigation | ≈340 px with readable labels | Expand for focus, collapse without rebuilding page |
| Tile geometry | Normal video ≈352×198 (16:9) | Original 16:9 card with one rounded white focus outline |
| Rail spacing | ≈24 px; x≈356,732,1108 expanded | Horizontal smooth reveal with previous focused card restored |
| Title/metadata | 1–2 lines + muted channel, views and age | Bound text length, never retain full server responses |
| Duration | Small bottom-right video badge | UI-only metadata; no downloaded video thumbnails |
| Album art | ≈200×200 cards | Distinct square cards only where appropriate |
| Search | History chips + native Switch keyboard | Device keyboard, independently clearable local search history |
| Settings | Left list ≈x98..378, vertical divider x393 | Right contextual detail; no OAuth/device linking |
| Channel | Full-width dimmed banner and horizontal shelves | Light, lazily displayed banner; **local** Follow action |
| Following | Channel list left, normal video tiles right | On-device followed channels; no Google sync |
| Library | History/Watch Later/Playlists rails | Canonical IDs, timestamps and resume; ≈2 MB max metadata target |

## Actual captured video playback

The second screenshot batch establishes the player overlay and right-side
settings geometry which the earlier NSP-only review lacked.

| Item | Observed 1280×720 coordinates | Implementation direction |
|---|---|---|
| Fullscreen video | Entire 1280×720 | Reuse hardware-tested M3 visible mpv render pipeline |
| Video heading | Title x≈64, top≈68 | Unobtrusive upper overlay on active controls |
| Timeline | x≈64 to1216, center y≈474 | Seek hitbox thicker than visual progress line |
| Transport | Central pause, previous, next near y≈527 | Controller-safe focus; never restart/stop unintentionally |
| Related videos | Horizontal row begins y≈598 | Normal videos only; no Shorts shelf or automatic ad cards |
| Settings video | Inset x≈36,y≈145,w≈764,h≈430 | Change viewport without destroying active decoder |
| Settings panel | x≈836..1244 | Focused list row ~408×64, gap ~8 |
| Quality picker | Auto and 1080/720/480/360/240 in original | Auto and *available* selections <=720p, preserve actual FPS |
| Audio picker | Stable volume and Voice boost options | Show only if a real, tested audio DSP backend exists |
| Playback settings | Quality, captions, audio, speed, repeat, diagnostics, misc | Original compact menus; remove report, feedback and account clutter |

The capture proves the official UI shows these controls, but it does
**not** prove our Switch build supports all options. Never ship phantom
Quality, Audio, Captions, Speed or Voice boost controls; hide/disable
until actual decoding/transport handling and tests are in place.

The overlay is not the playback engine. It must be showable and
dismissable while the media worker and libmpv remain alive; avoid
returning to the accepted black-screen issue. B closes the innermost
menu first, then the overlay, then exits playback as appropriate.
Pause and seek must not delete live files. Keep a visible
quality/captions toggle when supported, with focus restored on return.

## Features intentionally excluded

No Google/YouTube login or profiles, OAuth, device/TV pairing, Premium
upsells, purchases, Shorts, promoted videos, tracking links or Google
account-based likes/subscriptions. The app may save local favorites,
Watch Later, playlist membership, history, followed channels and
playback resume metadata, with no downloadable media library.

## Hard storage / memory constraints

- A low-RAM streaming worker uses ~25 MB maximum disposable rolling **SD**
  media storage, not a 25-MB RAM ring.
- NRO + resources + all persistent state and diagnostics + active
  streaming files <=100,000,000 decimal bytes.
- The rolling cache deletes media on stop and app restart cleanup,
  and must evict consumed media safely while decoding long videos.
- Never persist thumbnails or full YouTube metadata payloads.
- The image-screen examples cannot establish the official app's FPS or
  whether its decoder uses hardware acceleration. TizenTube NX targets
  up to 720p at the video's actual source FPS.

## Tested implementation checkpoint

The following original lightweight geometry models are now available:

- `include/tizentube_nx/ui/tv_layout.hpp`: sidebar widths, card
  geometry, square music cards and controller focus-scroll bounds.
- `include/tizentube_nx/ui/tv_player_layout.hpp`: fullscreen/inset
  player rectangles, progress hitbox, right settings panel, related
  tiles, bounded quality candidates, clamped seek progress.
- `tests/tv_layout_tests.cpp` and
  `tests/tv_player_layout_tests.cpp`: geometry, scaling, focus
  reveal, 16:9, layout boundaries and no-Shorts/no-account settings.

**Executed locally:** g++ C++20 `-Wall -Wextra -Wpedantic -Werror` on
both tests, normal `-O2` and ASan/UBSan `-O1 -g`—all passed.
CMake's host test suite now includes both. **Not validated:** full
devkitA64 native compilation, Borealis integration, physical UI tests.

The live M3 menu and accepted video player are intentionally
unchanged. This is geometry/reference engineering, not a released NRO.

## Safe integration order

1. Build and hardware-test the compact/expanded sidebar without
   losing joystick/D-pad/touch focus or Search Load More.
2. Bring in original lightweight video card rails, lazy thumbnail
   decoding and retained scroll position.
3. Replace always-visible M3 diagnostics/button rows with a
   reversible, video-first overlay based on these measured positions.
   Keep a diagnostics mode behind Settings.
4. Build small local History, Continue Watching and Favorites/playlist
   views, with bounded crash-safe metadata persistence and clear/pause.
5. Improve the low-RAM file-backed rolling stream and verify segment
   lifetime; only then release a combined candidate NRO.
