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

### Inline detail with destructive result rebuild — rejected overall

Checkpoint: `36015cb2bf9ae98fc983674563e9df0935232123`

Hardware proved that inline result detail itself works: A stays on Search, normalized ID and clean canonical URL render inline for upper/middle/lower results, and result-list controller focus remains usable.

The overall UI checkpoint is nevertheless **REJECTED**. After Search selection and return toward the sidebar, Search can become blank and sidebar traversal becomes trapped between Home and Search; Subscriptions, Library and Settings remain drawn but unreachable.

Pinned Borealis explains the two unsafe interactions removed by the next candidate:

- the inline implementation deleted/recreated the entire result subtree, including the focused result, then force-focused a newly-created result;
- B focused the Sidebar container rather than the active section item. `Application::giveFocus()` resolves the container to its default descendant (Home), so TabFrame synchronously removes the Search page. Re-entering Search rebuilt the selected result during the Sidebar focus callback and force-focused that unattached result, stealing focus away from the sidebar before traversal could continue below Search.

The replacement keeps result buttons alive and toggles pre-created detail labels non-destructively. B resolves the active section's actual SidebarItem, so returning to the sidebar no longer changes tabs or destroys the current Search page.

## Current physical gate

Candidate code checkpoint: `2642ba7ff559879222a6cddc2b236afbd07c0012`

This candidate fixes the rejected `36015cb...` focus/page-lifecycle behavior while preserving all accepted networking and first-page Search code.

Required physical acceptance:

- selecting top/middle/bottom results shows only that row's inline ID/clean URL without rebuilding the result tree;
- the focused result button remains the same live Borealis View;
- B keeps Search populated and focuses the Search sidebar item;
- Up reaches Home and Down traverses Search -> Subscriptions -> Library -> Settings;
- opening each non-Search section and returning to Search reconstructs current SearchModel results safely;
- a second Search still works;
- normal exit/relaunch still works.

`Load more` remains intentionally disabled until this full UI/navigation gate passes.

## Invariants for every gate

- Shorts/reels do not exist in the app.
- Ads/promoted/shopping renderers are blocked.
- Exact current M2 outbound allowlist remains `www.youtube.com:443` only.
- Nintendo endpoints remain hard-denied before DNS.
- Remote thumbnails remain disabled until separately reviewed.
- `switch-curl` remains absent from the NRO.
- Google/YouTube authentication remains absent from M2.
