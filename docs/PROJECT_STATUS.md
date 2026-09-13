# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

Branch: `feature/m2-guest-browsing`  
Draft PR: #6 — `M2: guest browsing networking foundation`

## Accepted baseline entering this session

Latest fully green accepted checkpoint at session start:

`9a69ed7c46b0e5281224d43dc89971c46124c6c9`

At that checkpoint host configure/build/ctest and the devkitA64 NRO build were green. Scoped Search parsing, result normalization/dedupe, canonical URL hardening, pagination state, request/privacy hardening, the offline Search model, and the renderer firewall were already present. `switch-curl` was absent, the allowlist was exactly `www.youtube.com:443`, and live Search remained disabled.

## Normalized Search metadata hardening

This session completed the previously partial optional-metadata slice without enabling networking in Search.

### Video results

Legacy `videoRenderer` and supported modern video `lockupViewModel` forms now normalize safely available:

- channel/owner name;
- channel ID from an unambiguous browse endpoint;
- bounded thumbnail candidates with dimensions;
- duration text and exact seconds when deterministic;
- view-count text and an exact count only when parsing is unambiguous;
- published/upload-age text;
- bounded accessibility text;
- live state;
- upcoming state;
- exact scheduled start time when a unique bounded integer is present.

Malformed or missing optional metadata does not invalidate an otherwise valid video result. Conflicting channel IDs or scheduled-start values are treated as unknown rather than guessed.

### Channel results

Legacy channel renderers and supported modern channel lockups normalize:

- channel ID;
- title/name;
- thumbnails;
- subscriber-count text plus exact numeric count when deterministic;
- video-count text plus exact numeric count when deterministic;
- bounded accessibility text.

Missing subscriber/video counts remain unknown without hiding the channel.

### Playlist results

Legacy playlist/radio renderers and supported modern playlist lockups normalize:

- playlist ID;
- title;
- owner/channel name;
- owner/channel ID when an unambiguous browse endpoint is available;
- video-count text plus exact numeric count when deterministic;
- thumbnail candidates;
- bounded accessibility text.

A missing count does not invalidate the playlist.

### Exact numeric parsing policy

Exact numeric fields are intentionally conservative. Plain ASCII decimal values and correctly comma-grouped values with a small reviewed English unit suffix may be converted to integers. Localized, abbreviated, malformed, overflowing or conflicting values remain presentation text only.

Tests cover malformed grouping, localized count strings, abbreviated counts, uint64 overflow, malformed scheduled time and conflicting scheduled times.

## Thumbnail normalization

Thumbnail handling is still metadata-only: no image request is issued and no host is allowlisted.

Normalized candidates are bounded and deterministic:

- parser-side collection is bounded;
- normalized consideration is bounded;
- output is capped at eight candidates;
- candidate URL length is bounded;
- malformed/non-HTTPS references are ignored;
- width/height are retained when valid;
- duplicate URLs are removed;
- preference ordering uses pixel area, then width/height, then stable URL ordering.

The compatibility `thumbnail_url` mirrors the preferred normalized candidate.

## Pure Search presentation helpers

Host-tested renderer-independent helpers now provide:

- duration formatting;
- LIVE / UPCOMING labels;
- optional view-count display;
- upload-age display;
- optional video-count display;
- graceful empty output when values are unknown.

Renderer-specific YouTube parsing is not placed in Borealis/UI code.

## Offline SearchModel contract

`SearchModel` remains transport-free and now explicitly covers:

- idle;
- loading;
- ready;
- empty;
- error;
- loading-more;
- end-of-results;
- retryable failure;
- continuation availability;
- selected stable result identity;
- stale-generation rejection.

Continuation failures can be explicitly retried while preserving continuation ownership. Non-retryable malformed/unsupported responses do not enter a blind retry loop.

## UI-facing Search error taxonomy

Search now has a coarse UI-safe error classification separate from technical diagnostics:

- network unavailable;
- network-policy rejection;
- HTTP failure;
- malformed response;
- unsupported response;
- empty results;
- continuation failure.

The executor classifies internal failures before sanitizing technical diagnostics, so coarse classification is retained without leaking tokens, visitor/session values, cookies, request/response bodies or authorization material to the future UI.

## Search robustness / firewall state

The scoped Search boundary remains unchanged: only the recognized first-page primary container and direct recognized continuation item arrays may reach the internal renderer walker.

The hard renderer firewall still runs before normalization. Shorts/reels, ads/promoted objects, shopping/product subtrees and unsupported renderers remain blocked. Tests explicitly attach valid-looking metadata to blocked Shorts/ad/shopping objects and verify that none of it escapes into normalized results.

Parser/resource limits remain explicit for response size, depth, nodes, renderer records, object members, array elements, decoded strings, extracted text, text runs, thumbnail candidates and thumbnail URL length.

## Future feature compatibility

The normalized data and presentation boundaries preserve clean seams for later optional features such as SponsorBlock, alternative/DeArrow-style metadata, playback-quality preferences, original-audio preference, Return YouTube Dislike integration, clean sharing and player cleanup. No such service is enabled or allowlisted in this M2 slice.

Shorts remain a hard project rejection and are not made configurable by any future reference architecture.

## Hard network safety invariant

**TizenTube NX must never intentionally resolve or connect to Nintendo network endpoints.**

This remains release-blocking and independent of 90DNS:

- outbound networking is default-deny;
- exact M2 allowlist remains `www.youtube.com:443` only;
- policy evaluation happens before DNS/connect/TLS/HTTP;
- Nintendo domain families are hard-denied;
- HTTP, alternate ports, IP literals, userinfo and malformed authorities are rejected;
- every redirect is revalidated before another DNS lookup;
- `switch-curl` remains excluded from the NRO.

No new host was authorized in this session.

## Switch UI networking state

The app still makes no hidden YouTube request at startup. The Home action `Test YouTube guest connection` remains the only deliberately live M2 probe and still targets the existing allowlisted `https://www.youtube.com/sw.js_data` bootstrap endpoint.

**Live Search remains disabled pending the real-Switch `Test YouTube guest connection` result.**

The Search UI must not perform the POST until that hardware result is supplied and accepted.

## Validation rule for the next accepted checkpoint

Entering accepted checkpoint: `9a69ed7c46b0e5281224d43dc89971c46124c6c9`.

The metadata-hardening session may replace it only after the exact final documentation/code HEAD passes:

- host configure;
- full host build;
- full host `ctest` suite;
- devkitA64 NRO build and artifact upload;
- network/linkage/UI-gate review.

Documentation commits themselves do not become accepted merely because an earlier code-only head was green.

## Remaining external blocker

The required external result remains the physical Atmosphere Switch result from:

`Test YouTube guest connection`

Expected successful non-sensitive status: `Guest ready`.

Only after that result is accepted should M2 connect the already host-tested Search execution path to a live Switch Search POST.
