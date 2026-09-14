# M2 Channel hardware rejection — 2cfac03

## Status

Hardware candidate:

`2cfac03bd1031ca47a3a8eb5a8197fd44dd600a4`

NRO SHA-256:

`dddad102562e9909e516dd4ce0ce09bb366213b98fa7fe513126748b74f129e6`

Physical result: **HARD REJECTED**.

Do not ask the user to retest this NRO.

## Physical Switch observations

The candidate booted and the already-accepted guest/Search path remained usable until Channel presentation was opened.

Observed sequence:

1. Reach `Guest ready`.
2. Open Search and obtain a normalized Channel result.
3. Existing inline Search result detail works.
4. Activate explicit `Open channel`.
5. The screen becomes blank.
6. Pressing B briefly exposes Channel-looking content during the transition.
7. The app then remains on a blank screen and normal UI navigation does not recover.
8. Pressing A from the blank state can still open the Search software keyboard.
9. While the keyboard is open it is effectively the only visible UI.
10. Closing the keyboard returns to the same blank screen.
11. No Atmosphere fatal/crash screen was observed.

## Interpretation

This is a UI/activity/view-lifetime failure, not a proven transport/parser failure.

The process remains alive and some Search callbacks still fire, but the visible Borealis hierarchy/presentation state is corrupted after the pushed Channel activity path.

The current implementation uses a separate `ChannelActivity` with `brls::Application::pushActivity()` / `popActivity()`. The symptom strongly resembles the earlier hardware-rejected dedicated Search-detail Activity at `5e566c0a4a8c2b5c040686bcc95a305c675cc4ce`, where content appeared only briefly during the pop transition. Treat that as a presentation/lifecycle precedent, not proof of an identical low-level root cause.

Do **not** respond by loosening the Channel parser, changing the YouTube request, widening renderer traversal, or adding another pushed Activity.

## Required recovery architecture

Replace pushed Channel presentation with an in-page Channel mode owned by the already-proven Search page / `ShellActivity` lifetime.

Target flow:

`Search -> explicit Open channel -> same Search ScrollingFrame shows Channel mode -> B -> same Search page restored`

Requirements:

- no Channel `pushActivity()`;
- no Channel `popActivity()`;
- no separate Channel Activity;
- Search query/results/continuations remain alive;
- Channel worker/model/parser remain renderer-independent and own no Borealis Views;
- Channel UI mutation remains UI-thread-only;
- B while Channel mode is visible returns to Search before normal sidebar routing;
- restore focus using the originating stable Search identity after layout;
- opening/closing the software keyboard after returning must restore a visible Search page;
- all five sidebar sections must remain reachable after Channel return.

## Preserve the existing Channel core

Unless an independent defect is proven, preserve:

- `ChannelModel` and generation protection;
- stale completion rejection;
- explicit guest Channel request;
- selected-tab scoped parser;
- Video/Playlist normalization;
- Shorts/reels terminal firewall;
- ads/promoted/shopping/Premium terminal firewalls;
- opaque unknown wrappers;
- bounded structural diagnostics;
- worker exception containment.

## Protected baselines

Search hardware baseline:

`428497c90be6e768c607e45c68171827d74f0faf`

Home FeedNudge UI baseline:

`ca8467e459389c22afee4f26fbebcddc858634a8`

Current M2 network policy remains exactly:

`www.youtube.com:443`

Nintendo destinations remain hard-denied before DNS. Remote thumbnails, playback, Google auth, Channel pagination and Playlist-page opening remain disabled for this recovery gate.

## Acceptance rule

Channel hardware acceptance remains **NO** until the in-page replacement is physically tested and proves:

- visible Channel loading/result/diagnostic state;
- no blank screen;
- B returns to the original populated Search page;
- original query/results remain intact;
- normal A/keyboard behavior remains usable afterward;
- B subsequently reaches the sidebar;
- all root sections remain reachable;
- repeated Channel open/back does not corrupt focus or presentation.
