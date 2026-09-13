# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

Branch: `feature/m2-guest-browsing`  
Draft PR: #6 — `M2: guest browsing networking foundation`

## Accepted baseline entering this extended session

Previous fully green checkpoint:

`72ef8db459ec0911adfc15abbfbafc706130e615`

At that checkpoint host tests/build and the devkitA64 NRO build were green, scoped Search parsing was integrated, the renderer firewall was intact, `switch-curl` was absent, the allowlist was exactly `www.youtube.com:443`, and live Search remained disabled.

## Extended offline M2 hardening

This session continued only work that is safe to complete without the pending real-Switch networking result.

### Normalized Search result architecture

Search now has a renderer-agnostic `BrowseResult` presentation model with explicit Video / Channel / Playlist types. The model carries stable IDs, title, channel attribution slots, thumbnail/duration/metadata slots, accessibility text, and live/upcoming indicators without exposing raw renderer JSON to future UI/controller code.

Missing metadata remains empty. Presentation strings and thumbnail/duration values are bounded/sanitized before normalized UI use.

### Result identity / dedupe

Stable identity is type-qualified (`video:`, `channel:`, `playlist:`). Deduplication is deterministic first-wins and happens after the hard renderer firewall, so a blocked Short/ad/shopping object cannot consume an identity belonging to legitimate content.

### Canonical YouTube links / validation

Normal video sharing is generated only as:

`https://www.youtube.com/watch?v=VIDEO_ID`

Tracking parameters, `si=`, `feature=`, timestamps, playlist contamination, redirect wrappers and source URL garbage are not retained. Stable channel-ID and playlist-ID canonical helpers are also available.

Normal video IDs are validated as 11 safe YouTube ID characters. Pasted YouTube watch/embed/live/youtu.be inputs are bounded and validated. Missing/duplicate/conflicting `v`, encoded garbage, deceptive/non-YouTube hosts, malformed authorities, empty/oversized inputs, and Shorts/reel routes fail closed. Shorts/reels are never canonicalized into ordinary videos.

### Search robustness corpus

Host tests now cover empty pages, missing ID/title, alternate text runs, malformed thumbnails, missing channel metadata, invalid duration text, live/upcoming/private-looking shapes, duplicates, mixed entity pages, missing/modern continuation tokens, unrelated actions, blocked ad/Shorts subtrees, out-of-scope renderer-shaped data, and unknown future renderers adjacent to legitimate siblings.

A deterministic mutation corpus adds malformed JSON, truncations, excessive nesting, oversized strings, randomized unknown renderer names, randomized out-of-scope video renderers, and blocked-subtree nesting. No heavyweight fuzzing dependency was added.

### Query / continuation state

`execute_guest_search()` remains stateless. `GuestSearchFlow` owns a logical query's pagination state:

- first request sends query only;
- continuation sends token only;
- transport/parse failure does not advance state;
- repeated/consumed token terminates pagination loops;
- missing token ends pagination;
- a new query clears all old continuation ownership.

This prevents stale query-A state being reused for query B.

### Request privacy / minimization

Guest Search remains on the exact reviewed endpoint:

`https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json`

Only the reviewed WEB/v1 guest client contract is accepted. Query, continuation and guest-session fields are bounded. User agent is no longer redundantly placed in Search JSON. Tests assert that official-client analytics/ad/tracking style fields are absent.

The retained context fields are client name/version, language, region, visitor data and timezone. Locale/region/timezone are kept for response shaping; InnerTube is private, so no claim is made that their exact purpose is publicly documented.

### Logging / diagnostic privacy

Normal Search/network paths do not dump response bodies, request bodies, cookies, authorization values, visitor/session identifiers, or continuation tokens. Public diagnostics are length-bounded. A diagnostic carrying sensitive markers is replaced wholesale with a generic safe failure instead of partially redacting potentially secret data.

### Offline UI model

`SearchModel` is host-testable and has no networking dependency. It provides explicit:

- idle
- loading
- ready
- empty
- error
- loading-more
- normalized result list
- continuation state
- stable selection identity
- generation tokens that reject stale async completions

This prepares the UI without activating Search networking.

### Parser trust-boundary cleanup

`parse_scoped_search_response()` is the intended application/network API. The lower-level recursive renderer-payload parser is now an explicitly internal build API and is guarded for host callers. This makes the safe scoped path the default and makes boundary bypass a deliberate internal action.

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

## Validation state

Previous accepted checkpoint: `72ef8db459ec0911adfc15abbfbafc706130e615`.

The extended-session commits are being validated continuously by host and devkitA64 CI. The final session head must pass the complete host configure/build/ctest workflow and the devkitA64 NRO workflow before it may replace the previous accepted checkpoint.

## Remaining external blocker

The required external result is still the physical Atmosphere Switch result from:

`Test YouTube guest connection`

Expected successful non-sensitive status: `Guest ready`.

Only after that result is accepted should M2 connect the already host-tested Search execution path to a live Switch Search POST.

## Later M2 work after hardware acceptance

- connect Search flow to the Switch worker path deliberately;
- validate first live Search response on hardware;
- render normalized results as Borealis cards;
- enable live continuation paging;
- add live Home/browse/channel/playlist presentation;
- complete real-hardware guest-browsing acceptance.

Account login, playback, SponsorBlock, DeArrow and all Shorts functionality remain out of scope for this checkpoint.
