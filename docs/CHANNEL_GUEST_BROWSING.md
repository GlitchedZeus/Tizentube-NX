# Channel guest browsing

## Scope

M2 Channel pages are explicit, first-page-only guest browse requests opened from an already-normalized Search `Channel` result. They reuse the app-lifetime native libnx/Horizon-SSL transport and the existing in-memory guest session. No Channel request occurs at startup or merely from highlighting a Search row.

## Entry and navigation

Search keeps its physically accepted A-to-toggle inline detail behavior. Selecting a normalized Channel result reveals a separate `Open channel` button. Activating that button pushes a plain `ScrollingFrame` Channel activity while the original Search page remains alive underneath. B/`Back to Search` pops only the Channel activity, so the query, loaded Search results, continuation state and existing Search View tree are preserved.

The Channel worker owns no Borealis Views. `ShellActivity::tick()` joins/publishes the plain normalized result on the UI thread and refreshes the active Channel activity only if it still exists. Backing out invalidates the Channel model generation, so a stale completion cannot mutate a later Channel request.

## Request boundary

A first-page request uses the stable normalized channel browse ID (`UC...`) from Search and the existing `/youtubei/v1/browse` request builder. Channel continuation is retained only as an opaque parser/model seam and is not exposed in the M2 first-page hardware gate.

The exact M2 network allowlist remains `www.youtube.com:443`. Remote avatars/banners/thumbnails, playback/media hosts and Google authentication remain unauthorized.

## Scoped parser

The parser selects exactly one provider-selected browse tab from `twoColumnBrowseResultsRenderer` or `singleColumnBrowseResultsRenderer`. It opens only reviewed structural wrappers (`richGridRenderer` / `sectionListRenderer`, `richItemRenderer`, `richSectionRenderer`, `richShelfRenderer`, `shelfRenderer`, `itemSectionRenderer`) and delegates only recognized normal leaf renderers to the existing bounded renderer/firewall parser.

Unknown wrappers are opaque. Unselected tabs, topbars, menus, command metadata and accessibility-only structures are never scanned for content. Shorts/reels, ads/promoted UI, shopping/product/merch and Premium/promo wrappers are terminal. A normal-looking `videoRenderer` with reel/Shorts navigation is still rejected by the shared renderer firewall.

If the selected physical Channel tab uses an unknown container family, the request fails as a structurally unsupported response and returns only a bounded family-name diagnostic. Raw JSON, IDs, titles, tokens, cookies, visitor data and URLs are never included in diagnostics.

## Metadata

Reviewed `channelMetadataRenderer` and legacy `c4TabbedHeaderRenderer` fields may provide optional title, handle, subscriber display text and description. The canonical Channel URL is generated locally from the validated requested channel ID. Missing optional metadata never fails an otherwise valid Channel page.

## Hardware gate

The first candidate must prove:

1. Guest ready.
2. Search for a query that returns a Channel result.
3. A toggles the existing inline detail unchanged.
4. `Open channel` performs the explicit browse request.
5. First-page normal results or a bounded unsupported-shape diagnostic render safely.
6. B returns to the still-populated original Search page.
7. Reopening a Channel does not crash, blank Search or corrupt focus.

Channel pagination, Playlist-page opening, playback, thumbnails and OAuth are deliberately deferred.
