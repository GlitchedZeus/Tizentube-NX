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
