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
- the UI reports more results are available
- no Shorts, ads, promoted or shopping result was observed

Only Video results were physically observed in this gate. Channel and Playlist normalization remain host-tested, not hardware-observed.

### Inline result detail + full sidebar traversal

Hardware-tested NRO head: `d6910a232732b2bd9169abb11dfdf320cf35a34b`

Underlying UI fix checkpoint: `2642ba7ff559879222a6cddc2b236afbd07c0012`

Physical Switch result: **ACCEPTED**.

Observed on hardware after the rejected `36015cb...` regression was fixed:

- inline normalized result detail works without a pushed black Activity;
- selecting results keeps Search usable;
- Search remains populated when returning toward the sidebar;
- B returns focus to the Search sidebar item;
- full sidebar traversal works through Home, Search, Subscriptions, Library and Settings;
- the previous Home/Search-only focus trap is gone;
- no blank Search page regression was observed;
- overall navigation/page lifetime behavior was reported as working perfectly on hardware.

The accepted design keeps result buttons alive and only toggles pre-created inline detail visibility. B targets the active SidebarItem rather than the Sidebar container/default Home descendant.

This acceptance closes the base Search UI/navigation lifecycle gate.

## Hardware rejected

### Dedicated pushed Search-result activity

Checkpoint: `5e566c0a4a8c2b5c040686bcc95a305c675cc4ce`

Pressing A on a Search result pushed a black blank screen. When B was pressed, the correct result detail screen appeared only for a fraction of a second during the pop transition before returning to Search.

The normalized result data itself was therefore present and correct, but the separate Borealis Activity presentation/navigation path failed its physical UI gate. This checkpoint is **not accepted** for result-detail UI.

### Inline detail with destructive result rebuild — rejected overall

Checkpoint: `36015cb2bf9ae98fc983674563e9df0935232123`

Hardware proved that inline result detail itself works: A stays on Search, normalized ID and clean canonical URL render inline for upper/middle/lower results, and result-list controller focus remains usable.

The overall UI checkpoint is nevertheless **REJECTED**. After Search selection and return toward the sidebar, Search can become blank and sidebar traversal becomes trapped between Home and Search; Subscriptions, Library and Settings remain drawn but unreachable.

Pinned Borealis explains the two unsafe interactions removed by the accepted replacement:

- the inline implementation deleted/recreated the entire result subtree, including the focused result, then force-focused a newly-created result;
- B focused the Sidebar container rather than the active section item. `Application::giveFocus()` resolves the container to its default descendant (Home), so TabFrame synchronously removes the Search page. Re-entering Search rebuilt the selected result during the Sidebar focus callback and force-focused that unattached result, stealing focus away from the sidebar before traversal could continue below Search.

The accepted replacement keeps result buttons alive and toggles pre-created detail labels non-destructively. B resolves the active section's actual SidebarItem, so returning to the sidebar no longer changes tabs or destroys the current Search page.

### Search continuation / Load more — functional, focus UX rejected

Hardware-tested NRO head: `959a46e7c1d5de1743cdf3b11a48b1b227ab0b4e`

Continuation implementation checkpoint: `534f576a77aeb8f7226b2f3a3549be23c621e06a`

Physical Switch result: **CONTINUATION DATA PATH WORKS, OVERALL UI GATE REJECTED**.

Observed on hardware:

- pressing `Load more` successfully fetches and appends additional videos;
- the app remains running and Search content is still recoverable;
- after the continuation append, the visible controller selector/focus indication disappears from the current viewport;
- the user must leave/re-enter Search or press Up to recover visible navigation;
- pressing Up after the selector disappears jumps back to the top/first Search result instead of moving naturally to the immediately preceding result;
- when navigating down to the `Load more` control and then back upward, focus jumps directly to the first video instead of the last result above `Load more`.

The current code places the persistent `Load more` button outside `search_results_box_` while result buttons live inside that nested box. Appending continuation rows grows the nested result box above an already-focused `Load more` button, and moving Up from that sibling crosses the nested-container boundary. This structure is a high-confidence focus/layout suspect and must be verified against pinned Borealis before the exact root cause is declared proven.

The continuation gate remains **NOT ACCEPTED** until focus remains visible and directional navigation is continuous across the last result / `Load more` boundary.

Pinned Borealis source confirms the focus mechanism: Up from the sibling `Load more` control enters the nested result Box through its default focus, which resolves to result 1, while appending rows above an already-focused `Load more` button moves that button without generating a new child-focus event for ScrollingFrame recentering. Fix candidate `d432ef8c6eee8e1b77ecd8a96b78832c43bb8a5b` keeps the append-only result tree, routes Load-more-Up explicitly to the current last result, and defers focus by stable identity to the first newly-added result until the next post-layout frame.

Also implemented in the same UI-fix slice: inline result detail is now a toggle. Pressing A on an unselected result opens its normalized detail; pressing A again on the same result closes it using `SearchModel::clear_selection()` and the existing non-destructive detail visibility path. This does not rebuild result Views.

### First continuation-focus fix — hardware rejected

Hardware-tested NRO head: `d9f33ffef915220f4deb31edb2108b39448bf200`

The continuation request/data path still works, and the explicit Up route remains useful, but the deliberate post-append focus transfer to the first newly-added result is **REJECTED on hardware**. Three physical-Switch screenshots show the sequence clearly:

- before `Load more`, the selector is visibly on the `Load more` button;
- after the append completes, the viewport shows the newly-appended tail and the `Load more` button, but no selector is visible;
- one Down press immediately scrolls back and selects the second newly-added result, with the first newly-added result directly above it.

This proves the new rows were appended correctly and focus had moved to the intended first newly-added row, but pinned Borealis did not recenter the `ScrollingFrame` viewport to that programmatic focus after the nested result box changed height. The selector was therefore alive but off-screen.

The next candidate removes that programmatic focus steal. After a successful non-terminal continuation, `Load more` remains the visible focused control; its refreshed custom Up route targets the actual current last result. Terminal continuation behavior keeps the existing last-result fallback before hiding `Load more`. No parser, request, network-policy or security behavior changes in this fix.

## Current physical gate — Search continuation focus polish

The next candidate must preserve the already-proven continuation request/data path while fixing only the Borealis focus/scroll behavior:

- `Load more` must fetch and append additional normalized results without losing the visible selector;
- after append, focus must remain on a visible stable control or be moved deliberately to the first newly-added result;
- Up from `Load more` must go to the last result immediately above it, not the first Search result;
- Down from the last result must reach `Load more` naturally;
- pressing A twice on the same Search result must toggle inline detail open then closed without deleting/recreating the row;
- B/sidebar traversal, first-page Search, accumulated result state and all previously accepted lifecycle behavior must remain unchanged;
- no network/parser/security policy changes are part of this gate.

## Invariants for every gate

- Shorts/reels do not exist in the app.
- Ads/promoted/shopping renderers are blocked.
- Exact current M2 outbound allowlist remains `www.youtube.com:443` only.
- Nintendo endpoints remain hard-denied before DNS.
- Remote thumbnails remain disabled until separately reviewed.
- `switch-curl` remains absent from the NRO.
- Google/YouTube authentication remains absent from M2.
