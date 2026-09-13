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

### Search continuation / Load more — fully accepted

Hardware-tested NRO head: `428497c90be6e768c607e45c68171827d74f0faf`

Physical Switch result: **ACCEPTED**.

This closes the full M2 Search hardware slice. Hardware proved that explicit continuation fetch/append works, the selector transfers to the first newly-loaded result after layout, the viewport follows correctly, Up/Down traversal across the continuation boundary is continuous, inline A/A-again detail toggling still works, and sidebar navigation remains intact. The preceding absolute-pixel `ScrollingFrame` backport was also stress-tested through **156 loaded results** without the earlier disappearing-selector or repeated-pagination exit.

The accepted Search implementation is now a protected baseline for later M2 surfaces.

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

### Second continuation-focus fix — hardware rejected; root cause narrowed

Hardware-tested NRO head: `c55abd6ffc09e72385fff478fefff06fed8a0473`

Physical Switch result: **REJECTED**. Keeping focus on the persistent `Load more` control did not fix the disappearing selector. Repeating `Load more` three times reproduced the same off-screen/blank-focus behavior. During further stress testing, roughly four to five continuation loads (about 60–80 visible results) also caused the app to exit unexpectedly, so repeated-pagination stability is now part of this gate rather than being treated as cosmetic focus polish.

The second hardware result disproves the prior assumption that the main problem was only programmatically transferring focus to the first newly-added row. The deeper issue is the pinned Borealis scroll representation itself: its `ScrollingFrame` stores `scrollY` as a **0..1 fraction of content height**, while the detached content already has an absolute pixel translation applied. When Search grows the nested result box, content height changes but the stored fraction and applied translation no longer describe the same viewport. Focus coordinates and visible scrolling can therefore diverge even when focus never changes.

A mature Switch Borealis fork used by StreamFin independently moved this logic to an **absolute pixel content offset** (`contentOffsetY`) and applies translation directly from that pixel offset. The next candidate backports that representation locally without upgrading the whole pinned framework, then explicitly re-runs centering after continuation layout. It also bounds the temporary text UI to the newest 40 live result rows while keeping all normalized loaded results in `SearchModel`, preventing unbounded Borealis/Yoga view growth during repeated-pagination stress.

The unexpected exit is not yet attributed to a specific allocator, renderer, network or focus failure without a crash log. The bounded live view tree is therefore a defensive stability measure, not a claim that memory pressure was definitively the crash cause.

### Pagination scrolling/stability foundation — hardware accepted

Hardware-tested NRO head: `57eac3c19fd19faa24a41d92dd60aa948b832d6b`

Physical Switch result: **SCROLL/STABILITY FIX ACCEPTED; FINAL POST-LOAD FOCUS UX STILL PENDING**.

Observed on hardware:

- repeated `Load more` remained stable through **156 loaded results**;
- the previous disappearing/off-screen selector problem did not recur;
- the earlier roughly 60–80-result unexpected exit did not recur during this stress run;
- selecting visible loaded results still opens/toggles their inline normalized detail correctly;
- the bounded 40-row hardware view remained usable while `SearchModel` retained the larger accumulated result set.

This hardware result strongly validates the local absolute-pixel `ScrollingFrame` backport and bounded live-view tree as the fix for the prior dynamic-content scroll divergence/stability problem. It does **not** yet accept the overall continuation UX.

One polish issue remains: after `Load more` succeeds, focus intentionally remains on the `Load more` button while the newly appended results appear immediately above it. The user therefore has to press/scroll Up to discover the new page. The next candidate should preserve the now-stable pixel scroll state but defer focus by stable result identity to the **first newly-added visible result** after layout.

## Current physical gate — guest Home first page

Search continuation is fully hardware accepted at `428497c90be6e768c607e45c68171827d74f0faf`. The next gate is deliberately narrower and must not reopen accepted Search behavior.

The Home candidate must prove on real hardware that:

- startup still performs no hidden YouTube Home request;
- after the explicit guest bootstrap reaches `Guest ready`, `Load Home` performs the first `FEwhat_to_watch` browse request;
- normal Video / Channel / Playlist results render as text-only controller-focusable rows where YouTube provides them;
- no Shorts/reels, ads/promoted content, shopping/product content, Premium/promo wrappers or unsupported renderer descendants reach the Home model;
- A toggles normalized stable ID + clean canonical URL inline without deleting the focused row;
- B returns to the Home SidebarItem and all five sidebar sections remain reachable;
- leaving and returning to Home safely reconstructs from `HomeModel` without stale Borealis View pointers;
- the accepted Search first page + Load more focus handoff still work afterward;
- remote thumbnails remain disabled and Home continuation remains deferred until this first-page gate passes.

Home hardware acceptance remains **NO** until this sequence passes on the physical Switch.

## Invariants for every gate

- Shorts/reels do not exist in the app.
- Ads/promoted/shopping renderers are blocked.
- Exact current M2 outbound allowlist remains `www.youtube.com:443` only.
- Nintendo endpoints remain hard-denied before DNS.
- Remote thumbnails remain disabled until separately reviewed.
- `switch-curl` remains absent from the NRO.
- Google/YouTube authentication remains absent from M2.

### Guest Home first page — zero-result diagnostic gate

Hardware-tested NRO head: `ba0066b34270bfa1809dc5d3567aead86a298033`

Physical Switch result: **REJECTED**. Guest bootstrap, sockets, SSL and verified HTTPS all succeeded, and the explicit `Load Home` action completed, but the UI reported `YouTube Home returned no supported normal results.` No Home rows were rendered. This is therefore treated as a Home response-shape/parser-scope problem, not a guest-network failure.

The next candidate adds bounded structural diagnostics only: browse layout, tab/selected-tab counts, reviewed renderer/container family names and counts, blocked-category counts, continuation-renderer counts, opaque-wrapper counts, and at most 16 bounded opaque family names. It never exposes titles, IDs, URLs, continuation tokens, visitor data, API keys, cookies, request headers or raw response bodies. Unknown wrappers remain opaque to normalization, and the diagnostic scanner also stops at them instead of inspecting descendants.

The exact M2 network allowlist remains `www.youtube.com:443`. Search checkpoint `428497c90be6e768c607e45c68171827d74f0faf` remains a protected accepted baseline. Home continuation, thumbnails, playback and OAuth remain disabled for this diagnostic gate.
