# Hardware gates

This file records physical Nintendo Switch acceptance separately from host/CI validation. A feature is not called hardware accepted until it has been exercised on the real Atmosphere Switch.

## Accepted

### Guest bootstrap

Checkpoint: `96dbf0be46d7fddb38a5510d9269d267e74fcce2`

Observed on hardware:

- `Connection status: Guest ready`
- `Sockets: existing | SSL: ready`
- verified HTTPS and bounded YouTube guest bootstrap succeeded

This accepts the borrowed Borealis BSD socket environment, SSL setup, outbound policy, DNS, TCP, TLS verification, HTTPS and memory-only guest-session bootstrap.

### Startup recovery

Checkpoint: `dbe1de9e0aa278999479dd3eac7a04d37b005b58`

The recovery NRO boots successfully on the real Switch. The earlier pre-first-frame `std::abort (0xFFE)` root cause remains unknown and must not be retroactively attributed without evidence.

### First-page live Search

Checkpoint: `a5fb638abd6e41d82490a460f41ea096b11d1ea5`

Observed on hardware for query `nintendo switch homebrew`:

- 15 normalized first-page results
- controller navigation/focus works
- a selected normal video exposes its normalized video ID
- the generated canonical URL is clean `https://www.youtube.com/watch?v=VIDEO_ID`
- the UI reports more results are available while `Load more` remains disabled
- no Shorts, ads, promoted or shopping result was observed

Only Video results were physically observed in this gate. Channel and Playlist normalization remain host-tested, not hardware-observed.

## Hardware rejected

### Dedicated pushed Search-result activity

Checkpoint: `5e566c0a4a8c2b5c040686bcc95a305c675cc4ce`

Pressing A on a Search result pushed a black blank screen. When B was pressed, the correct result detail screen appeared only for a fraction of a second during the pop transition before returning to Search.

The normalized result data itself was therefore present and correct, but the separate Borealis Activity presentation/navigation path failed its physical UI gate. This checkpoint is **not accepted** for result-detail UI.

## Current physical gate

Implementation checkpoint: `4cea7bdf50d3c2bae781633dd7c587c37aef11e3` (`search: show selected result details inline`).

The separate result Activity is removed. The accepted Search page now handles selection inline:

1. A selects the normalized result.
2. The click callback returns normally.
3. On the next UI tick, the result list is rebuilt.
4. The selected result's normalized ID and clean canonical URL appear directly beneath that result.
5. Controller focus is restored to the same result card.

The one-tick defer avoids deleting/replacing the focused Borealis button while its own A callback is still executing. The inline detail uses only the already-normalized in-memory result and performs no extra network request.

Physical acceptance still required:

- no black screen when A is pressed
- detail appears inline for a top result
- detail appears inline for a middle/lower result
- focus remains usable after expansion
- selecting another result moves the detail to that result
- B retains normal Search-page `Sections` behavior
- normal exit/relaunch still works

`Load more` remains intentionally disabled until this UI-only gate passes.

## Invariants for every gate

- Shorts/reels do not exist in the app.
- Ads/promoted/shopping renderers are blocked.
- Exact current M2 outbound allowlist remains `www.youtube.com:443` only.
- Nintendo endpoints remain hard-denied before DNS.
- Remote thumbnails remain disabled until separately reviewed.
- `switch-curl` remains absent from the NRO.
- Google/YouTube authentication remains absent from M2.
