# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

Branch: `feature/m2-guest-browsing`  
Draft PR: #6 — `M2: guest browsing networking foundation`

The real-Switch guest bootstrap, startup-recovery, and first-page live Search gates are accepted. The accepted first-page Search checkpoint is `a5fb638abd6e41d82490a460f41ea096b11d1ea5`; the next UI-only checkpoint fixes result-detail visibility without changing Search networking.

## Real-hardware guest-bootstrap acceptance

The native NRO boots successfully on the physical Atmosphere Switch.

The first explicit guest-connection test originally stopped before DNS/TCP/TLS/HTTP with:

`socketInitializeDefault failed: 0x00000f59`

That result is libnx `LibnxError_AlreadyInitialized`.

The source was proven: pinned Borealis commit `20e2d33b6c4ffce139ce304c503c04f5b94da920` initializes BSD sockets from its Switch `userAppInit()` before TizenTube NX reaches `main()`, and owns the matching `socketExit()` from `userAppExit()`.

TizenTube NX was changed to borrow the existing Borealis socket environment safely, while retaining separate SSL service lifetime/ref-count ownership.

The follow-up physical-Switch build at code checkpoint:

`96dbf0be46d7fddb38a5510d9269d267e74fcce2`

then passed the explicit guest connection test with:

`Connection status: Guest ready`

and safe detail:

`Sockets: existing | SSL: ready`

`Verified HTTPS and the bounded YouTube guest bootstrap both succeeded.`

The footer reported:

`M2 guest | Guest ready`

Therefore the physical hardware has verified the complete guest bootstrap path through:

- borrowed BSD socket environment;
- SSL service initialization;
- outbound-policy acceptance;
- DNS;
- TCP;
- TLS configuration/handshake;
- peer CA, hostname and certificate-date verification;
- HTTPS request/response;
- bounded YouTube guest bootstrap parsing;
- usable guest session creation.

This acceptance remains the prerequisite for live Search. It does not itself accept live Search.

## Native network lifetime model

`LibnxHttpClient` distinguishes cleanup ownership explicitly.

### BSD sockets

- `socketInitializeDefault()` success: socket environment is usable and TizenTube NX owns the matching `socketExit()`.
- exact `MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)`: socket environment is usable but borrowed; TizenTube NX does **not** call `socketExit()`.
- any other initialization error: fatal for this native client and preserved as an initialization failure.

The production code does not compare against hardcoded `0xF59`.

### SSL service

`sslInitialize(3)` uses libnx `ServiceGuard` reference-count semantics. A successful public `sslInitialize()` call acquires one matching `sslExit()` obligation even when another caller already holds an SSL reference.

TizenTube NX therefore tracks SSL separately from sockets.

### Application / worker lifetime

The native HTTPS client is owned by `main()` for application lifetime. `ShellActivity` and explicit request workers borrow it. Repeated guest-test or Search button presses do not create service init/exit cycles.

See `docs/NATIVE_NETWORK_LIFETIME.md` for the ownership analysis.

## Live Search implementation

Live guest Search is now wired into the Switch application without bypassing the existing core architecture.

The active path is:

`Borealis Search UI -> SearchModel -> GuestSearchFlow -> worker thread -> main-lifetime LibnxHttpClient -> www.youtube.com Search POST -> scoped Search parser -> Shorts/ad renderer firewall -> normalized BrowseResult -> Borealis text result UI`

The UI does not parse raw YouTube JSON and does not consume renderer-specific structures.

### Explicit query execution

Search remains user initiated:

1. choose `Enter search`;
2. enter/edit query with the Switch keyboard;
3. return to the Search page with the query visible;
4. press the explicit `Search` button;
5. the network request runs off the Borealis/UI thread.

Closing the keyboard does not automatically perform a Search.

The existing usable guest session is reused in memory. Visitor/session identifiers are not displayed, persisted or logged. If no usable guest session exists, the UI directs the user to the Home `Test YouTube guest connection` action.

### Worker and stale-result safety

Only one live Search worker is allowed at a time. Search and the manual guest-bootstrap diagnostic are not run concurrently.

`SearchModel` remains UI-thread-owned. `GuestSearchFlow` is used by the Search worker. Async completion publishes only normalized result/error data plus the captured `SearchModel` generation.

Editing the query or beginning a newer Search invalidates older generations, so a stale completion cannot replace newer results. Application shutdown joins both the guest diagnostic worker and Search worker before the app-lifetime native HTTP client is destroyed.

Borealis `TabFrame` destroys inactive tab pages. After the startup recovery, the Switch shell deliberately keeps the physically accepted plain `brls::ScrollingFrame` ownership model. `create_page()` clears UI-only pointers before constructing the replacement page, workers publish only model data, and Search UI refresh happens only on the main thread. Leaving Search while a request is in flight therefore does not give the worker any Borealis view pointer; reopening Search reconstructs the page from current `SearchModel` state.

### First page and result presentation

The existing `GuestSearchFlow::begin()` and `execute_guest_search()` path performs the real Search POST and routes the response through `parse_scoped_search_response()` and the existing renderer firewall.

Normalized Video / Channel / Playlist results are rendered as controller-focusable, text-only Borealis entries inside a scrolling Search page.

Where safely available:

- Video shows title, uploader/channel, duration, views, upload age, LIVE/UPCOMING state;
- Channel shows title, subscriber count and video count;
- Playlist shows title, owner/channel and video count.

Unknown optional metadata is omitted rather than fabricated as values such as `0 views`.

Remote thumbnails remain disabled. Search may normalize thumbnail references, but this UI does not fetch them and no thumbnail/CDN host was added to the outbound policy.

### Result selection seam

Selecting a result proves normalized routing without starting playback.

Hardware testing of checkpoint `a5fb638abd6e41d82490a460f41ea096b11d1ea5` exposed a presentation-only issue: the original detail label lived after the entire results box, so its normalized ID/URL was usually off-screen unless a result near the bottom of the first page was selected.

The follow-up UI opens a dedicated Borealis result-detail activity when A is pressed. It displays normalized title/type/ID and, when the stable ID is valid, a locally generated canonical clean URL immediately, with B returning to Search:

- Video: `https://www.youtube.com/watch?v=VIDEO_ID`
- Channel: canonical stable `/channel/CHANNEL_ID` URL
- Playlist: canonical `playlist?list=PLAYLIST_ID` URL

The detail activity consumes only the already-normalized in-memory result and performs no additional network request. No source tracking parameters are preserved.

### Continuation / Load more

The continuation core remains host-tested, but the first Search-reactivation hardware gate intentionally exposes **first-page Search only**. No `Load more` action is shown in this build.

The existing continuation flow, token ownership, repeated-token loop prevention, dedupe and retry behavior remain in core code/tests for a later hardware-gated slice. Disabling the UI action reduces page-lifetime and worker-state complexity while first-page Search is validated on the physical Switch.

### Error states

The Switch UI consumes the existing UI-safe Search error taxonomy rather than raw request/response data. It distinguishes policy/network/HTTP/parser/unsupported/continuation conditions through safe user-facing text.

The UI does not expose request JSON, response JSON, continuation tokens, cookies, authorization material, visitor IDs or session identifiers.

## Search hardening state

The previously host-tested M2 Search protections remain intact:

- scoped Search response boundary;
- hard Shorts/reel firewall;
- ads/promoted/shopping firewall;
- legacy and modern renderer normalization;
- bounded video/channel/playlist metadata;
- deterministic normalized thumbnail candidates;
- canonical clean sharing;
- query-owned continuation flow;
- request/privacy hardening;
- renderer-independent presentation helpers;
- `SearchModel` stale-generation, retry and end-of-results state;
- UI-safe Search error taxonomy;
- hostile and mutation/parser robustness corpus.

No unsafe generic parser fallback was added for live Search.

## V1 profile/account direction

V1 remains **local-first TizenTube profiles**.

Users should be able to create a TizenTube profile directly on Switch and build local follows/subscriptions, playlists, Watch Later, favorites/likes, history/resume and settings without Google authentication.

V1 must not request or store Google OAuth authorization. It must never collect Google passwords, passkeys, 2FA recovery codes, authenticated browser/YouTube cookies or exported cookie files.

No profile/account implementation is part of the current M2 Search slice.

## Post-v1 optional YouTube OAuth linking

The post-v1 account direction has been refined.

TizenTube NX may optionally link a real YouTube account using Google's official limited-input/device-code OAuth flow. The Switch should display a QR code plus a human-readable fallback code/instruction, while the phone opens Google's real sign-in and consent UI.

The design goal is that TizenTube NX never receives the Google password, passkey, 2FA recovery material or browser cookies.

The first linked-account implementation should:

- use the narrowest practical YouTube authorization;
- strongly prefer read-only access;
- initially sync one way: `YouTube -> TizenTube profile`;
- keep short-lived access tokens in memory where practical;
- optionally retain refresh authorization only when the user chooses `Keep me connected`;
- perform a dedicated secure-storage review before persistent authorization ships;
- never store persistent authorization as obvious plaintext on the SD card;
- fully redact tokens/authorization headers from logs, UI, exports, backups and crash diagnostics;
- preserve the local profile if authorization expires, is revoked or is lost;
- show `Reconnect YouTube` rather than destroying imported/local data;
- provide a distinct `Unlink YouTube` action that removes local authorization without deleting the TizenTube profile;
- keep local TizenTube actions local unless a future write-capable feature receives a separate security review and explicit consent.

A public/no-auth profile or playlist importer may remain as an optional fallback for users who do not want OAuth linking.

See `docs/PROFILES_AND_IMPORT.md` and `docs/OAUTH_ACCOUNT_LINKING.md`.

This OAuth work is post-v1 documentation only and must not widen the current M2 allowlist or enter the current implementation.

## Feature-reference policy

Morphe remains a first-class future feature reference through `AGENTS.md` and `docs/MORPHE_REFERENCE.md`. Morphe does not weaken the project-wide no-Shorts invariant.

No SponsorBlock, DeArrow, Return YouTube Dislike or other unrelated Morphe network integration was enabled by this Search milestone.

## Hard network safety invariant

**TizenTube NX must never intentionally resolve or connect to Nintendo network endpoints.**

This remains release-blocking and independent of 90DNS:

- outbound networking is default-deny;
- exact current M2 allowlist remains `www.youtube.com:443` only;
- policy evaluation happens before DNS/connect/TLS/HTTP;
- Nintendo domain families are hard-denied;
- HTTP, alternate ports, IP literals, userinfo and malformed authorities are rejected;
- every redirect is revalidated before another DNS lookup;
- `switch-curl` remains excluded from the NRO;
- no thumbnail/image CDN host was added;
- no Google-authentication endpoint is currently authorized.

No host is authorized merely because YouTube returns a URL for it. Future OAuth/API hosts require their own explicit outbound-policy review before DNS.

## Current activation gate

The real-Switch guest bootstrap remains accepted as `Guest ready`.

The startup-recovery NRO at checkpoint `dbe1de9e0aa278999479dd3eac7a04d37b005b58` is physically accepted and boots without the earlier pre-first-frame abort.

First-page live Search is now also **PHYSICALLY ACCEPTED** at checkpoint `a5fb638abd6e41d82490a460f41ea096b11d1ea5`. On the real Atmosphère Switch, the query `nintendo switch homebrew` returned 15 normalized live results, the UI reported that more results were available, controller navigation worked, and selecting a normal video produced its normalized video ID plus clean canonical `https://www.youtube.com/watch?v=...` URL. User-supplied screenshots show both the live result list and the selected-video detail.

The hardware test also found one presentation-only defect: the selection detail label was positioned after the whole first-page result list, making it difficult to see for non-bottom results. The next UI checkpoint replaces that list-bottom label with a dedicated result-detail activity opened by A and closed with B. This does not alter the accepted Search POST/parser/session/network path.

The original pre-first-frame `std::abort (0xFFE)` root cause remains **UNKNOWN**. Do not retroactively attribute it to Search networking, `bad_alloc`, or the worker hardening without new evidence.

`Load more` remains disabled until the result-detail UI checkpoint is verified and continuation is deliberately reintroduced as its own hardware-gated slice. Startup boot markers, remote-thumbnail blocking, and the exact `www.youtube.com:443` allowlist remain unchanged.

## Physical startup crash investigation — 2026-09-13

- First hardware attempt of checkpoint `5b46829ead0a94089696e1521cb101637e2bfb3e` aborted immediately from Spaira before the TizenTube NX UI became visible. Atmosphère reported `std::abort (0xFFE)`.
- No guest probe, Search tab interaction, keyboard, Search POST or live YouTube Search response occurred. Live Search hardware acceptance is therefore **FAILED / BLOCKED**, while the earlier guest-bootstrap checkpoint `96dbf0be46d7fddb38a5510d9269d267e74fcce2` remains physically accepted.
- The exact bad NRO was reconstructed byte-for-byte. Its hardware offsets were symbolicated against the matching ELF/map. The reported PC `+0x89568` resolves inside pinned Borealis `SwitchVideoContext::updateWindowSize()` to a normal `ldr x21, [sp,#32]` in the display-resolution fallback path; the mixed stack-scan candidates do not form a trustworthy unwind chain. Root cause remains **UNKNOWN**.
- Pinned Borealis creates TabFrame pages lazily. Initial focus creates Home; Search is not eagerly constructed, so `refresh_search_page()` is not on the normal pre-first-frame startup path.
- Accepted-vs-bad binary review found identical `.bss`/TLS sizes. The bad image added about 168 KiB of allocatable sections, mostly code, which does not support a startup OOM conclusion.
- Startup recovery build quarantines live Search UI/network execution, restores plain `ScrollingFrame` page ownership used by the accepted shell, and writes bounded last-stage boot markers through first-frame completion.
- Preventative Search hardening is retained separately: `std::bad_alloc`, `std::exception` and unknown exceptions are contained by a no-throw worker boundary; exception text is discarded; publication has a no-throw emergency error path; first-page flow state is reset on fatal-like worker failures. This is **not** claimed as the cause of the physical startup crash.
- Exact outbound policy remains `www.youtube.com:443`; Nintendo hard deny, TLS verification, redirect revalidation, IP-literal rejection and the absence of `switch-curl` remain unchanged.
- The startup-recovery NRO was subsequently tested on the real Atmosphère Switch and booted without crashing. Startup recovery is therefore **PHYSICALLY ACCEPTED** at `dbe1de9e0aa278999479dd3eac7a04d37b005b58`.
- First-page Search was subsequently exercised successfully on the physical Atmosphère Switch at `a5fb638abd6e41d82490a460f41ea096b11d1ea5`: the live query returned 15 results and a selected normal video showed its normalized ID and clean canonical URL. First-page live Search is therefore **PHYSICALLY ACCEPTED**. A presentation-only follow-up moves selection details into a dedicated activity because the original label was usually below the visible list. `Load more` remains disabled pending the next hardware-gated slice.
