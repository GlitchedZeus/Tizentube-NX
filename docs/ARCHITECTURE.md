# Architecture

TizenTube NX is a native Nintendo Switch homebrew client built around narrow trust boundaries. M2 remains guest-browsing infrastructure; live Search is intentionally not connected to the Switch UI yet.

## Core layering

The intended Search path is:

1. `GuestSession` + request builder create the minimal reviewed WEB guest request.
2. `HttpClient` performs transport. On Switch the implementation is direct libnx BSD sockets + Horizon SSL.
3. `parse_scoped_search_response()` validates the full JSON document and admits only recognized Search primary or continuation containers.
4. The internal renderer-payload parser traverses only those admitted containers.
5. The renderer firewall drops Shorts/reels, ads/promoted, shopping, and unsupported renderers before normalization.
6. `BrowseResult` normalizes allowed entities into Video / Channel / Playlist presentation data.
7. deterministic type-qualified identity deduplicates results.
8. `GuestSearchFlow` owns one query's continuation state and prevents cross-query token reuse.
9. `SearchModel` owns offline UI state only; it contains no HTTP client and cannot initiate networking.

Network/application code must call `parse_scoped_search_response()`. `browse_response.hpp` is guarded as an internal renderer-payload API so host callers cannot casually bypass the full Search scope boundary.

## Search result model

`BrowseResult` is renderer-agnostic. UI/controller code should use it instead of YouTube renderer names or raw JSON. It provides stable fields for supported normal content, including ID, title, channel attribution, thumbnail/duration/metadata text slots, live/upcoming flags, and type.

Missing metadata remains empty. TizenTube NX does not invent values. Presentation strings and thumbnail references are bounded before entering the normalized UI model; malformed duration/thumbnail metadata is dropped rather than making an otherwise legitimate sibling visible as malformed UI data.

Identity is deterministic and type-qualified:

- `video:<video-id>`
- `channel:<channel-id>`
- `playlist:<playlist-id>`

Deduplication happens only after the renderer firewall. A blocked Short/ad/shopping object therefore cannot consume an identity and suppress a legitimate normal result.

## Search response trust boundary

Initial Search results may originate only from:

`contents.twoColumnSearchResultsRenderer.primaryContents`

Continuation result arrays may originate only from direct recognized actions under:

- `onResponseReceivedCommands`
- `onResponseReceivedActions`
- `onResponseReceivedEndpoints`

and only through:

- `appendContinuationItemsAction.continuationItems`
- `reloadContinuationItemsCommand.continuationItems`

Renderer-looking JSON in topbars, headers, sidebars, menus, metadata, unrelated actions, or nested fake continuation commands is out of scope and cannot become a result.

Inside an accepted container, the renderer firewall still applies. Scope restriction is an additional layer; it does not replace Shorts/ad/shopping/unknown-renderer rejection.

## Pagination state

The low-level `execute_guest_search()` function remains transport-neutral and stateless. `GuestSearchFlow` owns the state for a logical query:

- a first request contains the query and no continuation;
- a continuation contains the opaque token and does not resend the query;
- failed HTTP/parse attempts do not advance or consume the token, so an explicit retry is possible;
- missing continuation ends pagination;
- repeated/already-consumed tokens end pagination to prevent loops;
- starting a new query clears the old query and all continuation ownership.

This keeps query A's continuation from leaking into query B.

## Canonical YouTube URLs

TizenTube NX generates URLs from validated stable identifiers and never preserves source tracking parameters.

Normal video:

`https://www.youtube.com/watch?v=VIDEO_ID`

Playlist:

`https://www.youtube.com/playlist?list=PLAYLIST_ID`

Channel:

`https://www.youtube.com/channel/CHANNEL_ID`

Shorts/reel routes are rejected and are never converted into a normal-video canonical URL.

## Request and diagnostic privacy

Guest Search uses the reviewed `www.youtube.com` WEB/v1 request contract only. Query, continuation, and session fields are bounded before request construction. The JSON context intentionally omits official-client analytics/ad/tracking bloat; user agent is an HTTP header and is not duplicated into the JSON body.

Current guest context keeps client name/version, language, region, visitor data, and timezone. Language/region/timezone are retained for Search response shaping; because InnerTube is a private protocol, their exact server-side necessity is not claimed as documented public behavior.

Normal diagnostics do not dump request/response payloads, cookies, authorization material, visitor/session IDs, or continuation tokens. Public-facing transport errors are bounded; diagnostics carrying sensitive markers are replaced wholesale by a generic safe error instead of attempting risky partial redaction.

## Network safety

The M2 outbound policy remains default-deny before DNS. The exact live allowlist is:

`www.youtube.com:443`

Nintendo domain families remain explicitly hard-denied. Redirects are revalidated before another DNS lookup. `switch-curl` remains excluded from the NRO; the approved transport is direct libnx sockets + the local Horizon SSL service with certificate verification.

90DNS is defense in depth, not authorization.

## Current activation gate

The only intentionally live M2 UI action remains the user-triggered Home `Test YouTube guest connection` bootstrap probe.

**Live Search remains disabled pending the real-Switch `Test YouTube guest connection` result.**

The host-tested Search executor, flow, and UI model do not change that gate.
