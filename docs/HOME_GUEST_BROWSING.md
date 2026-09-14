# Guest Home browsing

This document records the M2 first-page Home contract. It is intentionally narrower than the eventual polished YouTube/TizenTube/Morphe-style Home experience.

## Activation and network boundary

- App startup does **not** silently request YouTube Home.
- The user first performs the existing explicit guest bootstrap and reaches `Guest ready`.
- Home then exposes an explicit `Load Home` / `Refresh Home` action.
- The action reuses the app-lifetime `LibnxHttpClient` and the accepted in-memory guest session.
- The first request uses the existing `FEwhat_to_watch` browse request through `https://www.youtube.com/youtubei/v1/browse?prettyPrint=false&alt=json`.
- The M2 outbound allowlist remains exactly `www.youtube.com:443`; no image, media, auth or third-party host is added.
- Nintendo destinations remain hard-denied before DNS. HTTPS/443-only, redirect revalidation, IP-literal rejection and TLS verification remain unchanged.

## Scoped response boundary

The network-facing Home parser does not perform a generic recursive scan of the entire YouTube response. It selects only the recognized Home browse payload from the selected browse tab, plus reviewed continuation action arrays used as an architectural seam.

Inside that scope, the parser opens only reviewed structural wrappers and leaf renderer families. Unknown, Premium/promo, command-only and other unreviewed wrapper families remain opaque even if they contain a video-shaped descendant. This prevents an otherwise valid `videoRenderer` from escaping a blocked or unsupported container.

Reviewed first-page leaf families normalize into the existing renderer-independent `BrowseResult` model for normal Video, Channel and Playlist content. The existing hard firewall still rejects Shorts/reels, ads/promoted content and shopping/product content. Stable normalized identity drives deterministic dedupe.

Useful Home shelf/group labels are retained where safely available so later UI work can present YouTube-style shelves without making the temporary text UI part of the core data contract.

## Normalized state and Borealis lifetime

`HomeModel` is UI-thread-owned and contains normalized Home data, generation state, selected identity and bounded safe error text. Worker threads publish only normalized `HomePage` data plus the captured generation; they never retain Borealis View pointers.

This matters because pinned Borealis `TabFrame` destroys inactive pages. Leaving Home can therefore destroy its current Views while the model remains valid. Re-entering Home reconstructs the page from `HomeModel`, avoiding stale pointers and preserving the lifecycle lessons learned from the accepted Search implementation.

A newer Home generation rejects an older stale completion. Refresh keeps the last good Home result set visible while the replacement request runs, and a failed refresh does not require workers to mutate UI objects.

## First hardware-gate UI

The first Home candidate is deliberately text-only. Each accepted result is controller-focusable and uses the same normalized presentation helpers already proven by Search:

- Video: title plus optional channel, duration, views, upload age and live/upcoming state when safely available.
- Channel: title plus optional subscriber/video metadata.
- Playlist: title plus optional owner/video metadata.
- Unknown optional metadata is omitted instead of fabricated.

Pressing A toggles an inline normalized detail block containing the stable result identity and clean canonical URL. Pressing A again on the same result collapses the detail. The focused row is kept alive; selection does not destroy/recreate the result tree or push a second Borealis Activity.

B returns to the Home SidebarItem and must preserve full Home / Search / Subscriptions / Library / Settings traversal.

## Deliberately deferred

Remote thumbnail fetching is disabled even though bounded thumbnail candidates may be normalized in memory. No `i.ytimg.com`, `yt3.ggpht.com` or other image host is authorized by this slice.

The Home parser can retain an opaque continuation token, but Home pagination is not enabled for the first physical gate. The first page must pass real-Switch acceptance before Home continuation receives its own UI/hardware slice.

Playback, channel pages, playlist pages, profiles, OAuth, SponsorBlock, DeArrow, Return YouTube Dislike and other later features are outside this gate.

## Protected Search baseline

The complete Search slice is physically accepted through `428497c90be6e768c607e45c68171827d74f0faf`, including explicit continuation, first-new-result focus handoff, inline A/A-again detail toggle, sidebar traversal and the previously stress-tested 156-result scrolling foundation.

Home development must not reopen or redesign that accepted Search path unless a real regression requires it.

## Physical zero-result diagnostic follow-up

The first physical Home candidate at `ba0066b34270bfa1809dc5d3567aead86a298033` reached `Guest ready` and completed `Load Home`, but normalized zero supported results. The diagnostic follow-up does **not** loosen the renderer firewall or introduce generic result recursion. It reports only bounded structural renderer/container-family information from the selected Home scope and stops at blocked or unknown families. This is intended to identify the real modern Home wrapper returned on hardware before any new family is admitted to normalization.

Implementation checkpoint `8d4bbc7e58e80e711232cade97c7608b8b3e6e42` adds the privacy-safe structural diagnostic path. The next exact-head NRO produced after this documentation marker is a diagnostics-only physical candidate: Guest ready -> Load Home -> capture the bounded `Home diag:` summary. Home first-page content remains unaccepted until that hardware evidence identifies the actual response family.


## Physical guest empty-state acceptance

Diagnostic head `7b4993f7871ae71cb8ca44e30e51f876f8133cc2` proved the real Switch request path reaches the selected `FEwhat_to_watch` Home scope. The physical response was `twoColumnBrowseResultsRenderer -> selected tabRenderer -> richGridRenderer -> richSectionRenderer -> feedNudgeRenderer`, with zero normal Video/Channel/Playlist results and no observed Shorts/ad/shopping/promo renderer families.

`feedNudgeRenderer` is treated as a reviewed **terminal non-content** renderer and mapped to renderer-independent `HomeEmptyReason::FeedNudge`. Recognition is structural and localization-independent; no English title/subtitle text is parsed for semantics. The parser does not descend through the feed nudge, and feed nudges hidden in unknown/blocked/unselected/out-of-scope containers cannot set the typed reason. A nudge-only page is valid, empty, and terminal with no continuation. If normal reviewed results coexist, those results win and the empty marker is cleared.

The normal Switch UI presents a friendly guest-empty message rather than a structural diagnostic dump. Diagnostics remain available only for genuinely unsupported/unknown responses. This accepts the **Home request path and provider empty state**, not normal-result rendering; normal Home Video/Channel/Playlist rows are still host-tested but not physically observed. Channel pages are the next M2 implementation surface.


## Physical FeedNudge presentation acceptance

Exact-head NRO `ca8467e459389c22afee4f26fbebcddc858634a8` was physically tested after the typed empty-state cleanup. Guest bootstrap and explicit Home browse succeeded; the app displayed the friendly `YouTube isn't providing guest Home recommendations for this session. Use Search to get started.` message with no structural diagnostic dump and no crash. This accepts the Home request path, provider FeedNudge state and friendly UI presentation. Normal Home result rendering remains host-validated/not physically observed because YouTube supplied zero normal Home results.
