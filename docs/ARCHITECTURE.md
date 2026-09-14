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
7. deterministic type-qualified identity deduplicates results after the firewall.
8. `GuestSearchFlow` owns one query's continuation state and prevents cross-query token reuse.
9. `SearchModel` owns offline UI state only; it contains no HTTP client and cannot initiate networking.

Network/application code must call `parse_scoped_search_response()`. `browse_response.hpp` is guarded as an internal renderer-payload API so normal callers cannot casually bypass the full Search scope boundary.

## Normalized Search result model

`BrowseResult` is renderer-independent. UI/controller code consumes normalized fields rather than YouTube renderer names or raw JSON.

Supported optional fields include:

- stable result type, ID and title;
- channel/owner name and channel ID where an unambiguous browse endpoint supplies one;
- bounded thumbnail candidates with URL, width and height;
- preferred thumbnail URL;
- duration text plus exact parsed seconds when deterministic;
- view-count text plus exact parsed count when deterministic;
- upload/published-age text;
- subscriber-count text plus exact parsed count when deterministic;
- video-count text plus exact parsed count when deterministic;
- bounded accessibility text;
- live/upcoming state;
- exact scheduled start time when an unambiguous bounded integer is present.

Missing or malformed optional metadata never invalidates an otherwise valid result. TizenTube NX does not invent values. Localized, abbreviated or malformed count text may be retained for presentation while the corresponding numeric field remains absent. Conflicting navigation IDs or scheduled start values are treated as unknown instead of guessed.

### Legacy and modern parity

The normalizer accepts metadata from both currently supported families:

- legacy `videoRenderer`, `channelRenderer`, `playlistRenderer` / `radioRenderer`;
- modern `lockupViewModel` video, playlist and channel forms.

Modern and legacy paths normalize into the same `BrowseResult` contract. The UI therefore does not depend on which YouTube renderer family supplied a result.

## Thumbnail candidate model

Thumbnail work remains data-only in M2. No thumbnail is downloaded by this layer and no image/CDN host is added to the network policy.

Normalization rules are deterministic and bounded:

- parser-side candidate collection is bounded;
- normalized input consideration is bounded;
- normalized output is capped at eight candidates;
- URL length is bounded;
- only syntactically safe HTTPS references are retained;
- width/height are retained when valid;
- duplicate URLs are removed;
- preference ordering is known pixel area descending, then width, height, and stable URL ordering;
- malformed candidates are ignored without invalidating the result.

The old single `thumbnail_url` field mirrors the first normalized candidate for compatibility. Future image loading or DeArrow-style thumbnail replacement must be a separate reviewed adapter and must pass outbound policy before DNS.

## Pure presentation helpers

Search display helpers are pure host-testable functions. They format duration, LIVE/UPCOMING labels, exact numeric view/video counts when source display text is absent, and pass through safe upload-age text. They contain no renderer parsing and no Borealis/network dependency.

## Search result identity / dedupe

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

Inside an accepted container, the renderer firewall still applies. Scope restriction is an additional layer; it does not replace Shorts/ad/shopping/unknown-renderer rejection. Metadata attached to blocked content remains blocked with its parent object.

## Parser and metadata bounds

The Search parser keeps explicit response/depth/node/record bounds and also bounds object members, array elements, decoded strings, extracted text, text runs, thumbnail candidates and thumbnail URLs. Deterministic adversarial tests cover truncation, malformed JSON, excessive nesting, oversized metadata, unknown renderers, blocked-subtree nesting, malformed counts, numeric overflow, localized counts and conflicting optional values.

## Pagination and SearchModel state

The low-level `execute_guest_search()` function remains transport-neutral and stateless. `GuestSearchFlow` owns the state for a logical query:

- a first request contains the query and no continuation;
- a continuation contains the opaque token and does not resend the query;
- failed HTTP/parse attempts do not advance or consume the token;
- missing continuation ends pagination;
- repeated/already-consumed tokens end pagination to prevent loops;
- starting a new query clears old query/continuation ownership.

`SearchModel` is a pure offline controller/presentation state model. It explicitly represents idle, loading, ready, empty, error and loading-more, plus:

- end-of-results;
- continuation availability;
- retryable failure state;
- stable selected result identity;
- generation tokens that reject stale async completions.

A continuation failure can be explicitly retried without fabricating a new token. A malformed/unsupported response is not blindly treated as retryable.

## UI-safe Search error taxonomy

Future Search UI code receives a coarse `SearchErrorCode` rather than raw transport/parser text. Current categories distinguish:

- network unavailable;
- outbound-policy rejection;
- HTTP failure;
- malformed response;
- unsupported response;
- empty results;
- continuation failure.

Technical diagnostics remain separate and are sanitized before crossing the executor boundary. User-facing messages never include response bodies, request bodies, cookies, authorization values, visitor/session IDs or continuation tokens.

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

Normal diagnostics do not dump request/response payloads, cookies, authorization material, visitor/session IDs, or continuation tokens. Public-facing transport errors are bounded; diagnostics carrying sensitive markers are replaced wholesale by a generic safe error instead of attempting risky partial redaction.

## Future feature seams

The normalized result/presentation boundaries are intentionally compatible with later optional feature adapters such as SponsorBlock, alternative/DeArrow-style titles or thumbnails, playback-quality preferences, original-audio preference, Return YouTube Dislike data, clean sharing and player cleanup. None of those services are activated or allowlisted by this M2 work.

Shorts are not an optional presentation preference: Shorts/reels remain rejected before normalization and no future reference implementation may override that project invariant.

## Network safety

The M2 outbound policy remains default-deny before DNS. The exact live allowlist is:

`www.youtube.com:443`

Nintendo domain families remain explicitly hard-denied. Redirects are revalidated before another DNS lookup. `switch-curl` remains excluded from the NRO; the approved transport is direct libnx sockets + the local Horizon SSL service with certificate verification.

90DNS is defense in depth, not authorization.

## Current activation gate

The only intentionally live M2 UI action remains the user-triggered Home `Test YouTube guest connection` bootstrap probe.

**Live Search remains disabled pending the real-Switch `Test YouTube guest connection` result.**

The host-tested Search parser, executor, flow, normalized model, error taxonomy and offline UI model do not change that gate.
