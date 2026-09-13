# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

Branch: `feature/m2-guest-browsing`  
Draft PR: #6 — `M2: guest browsing networking foundation`

The real-Switch guest bootstrap, startup recovery, and complete M2 Search slice are physically accepted. Hardware-tested Search head `428497c90be6e768c607e45c68171827d74f0faf` closes the continuation focus gate: explicit Load more appends successfully, focus lands on the first newly-loaded result, the viewport follows, A-again collapses inline detail, sidebar navigation remains intact, and the underlying pixel-scroll foundation was previously stress-tested through 156 loaded results. Development has now moved to the first live guest Home page. Home is implemented as an explicit post-Guest-ready action with a scoped Home parser/model and text-only UI; it remains pending physical Switch acceptance.

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

The first follow-up attempted a dedicated Borealis result-detail activity at clean checkpoint `5e566c0a4a8c2b5c040686bcc95a305c675cc4ce`. Physical hardware rejected that presentation: pressing A produced a black blank pushed activity, while the correct normalized detail content became visible only for a fraction of a second during the B/pop transition. The underlying result data remained correct; this was a presentation/navigation failure.

Checkpoint `36015cb2bf9ae98fc983674563e9df0935232123` then proved that inline detail rendering itself worked, but its selection path rebuilt the entire result tree and force-focused a replacement result. Combined with focusing the Sidebar container on B, that caused a blank Search page and trapped sidebar traversal at Home/Search. That overall UI checkpoint was rejected.

The accepted replacement is the hardware-tested NRO at `d6910a232732b2bd9169abb11dfdf320cf35a34b`, with underlying UI fix `2642ba7ff559879222a6cddc2b236afbd07c0012`. Result buttons remain alive for selection; each result owns a pre-created inline detail label and A only changes detail visibility. The normalized type/title/ID and locally generated clean canonical URL appear directly with the selected result:

- Video: `https://www.youtube.com/watch?v=VIDEO_ID`
- Channel: canonical stable `/channel/CHANNEL_ID` URL
- Playlist: canonical `playlist?list=PLAYLIST_ID` URL

B now focuses the active SidebarItem instead of the Sidebar container/default Home descendant. Physical testing confirmed Search stays populated and full sidebar traversal through Home, Search, Subscriptions, Library and Settings works normally. The inline detail uses only already-normalized in-memory result data and performs no additional network request. No source tracking parameters are preserved.

### Continuation / Load more

Code checkpoint `534f576a77aeb8f7226b2f3a3549be23c621e06a` re-enables the existing continuation core in the Switch UI without replacing the accepted first-page Search path.

The candidate exposes an explicit focusable `Load more` button only when the current `SearchModel` owns a valid continuation. It reuses the app-lifetime `LibnxHttpClient`, current in-memory guest session, existing Search worker boundary, and `GuestSearchFlow::next()`; there is no hidden guest bootstrap, new network client, or second continuation implementation.

Continuation presentation is deliberately append-only. Existing first-page result buttons, metadata and inline-detail labels stay alive while the worker runs. After `SearchModel::apply_continuation()` performs stable-identity dedupe, only newly appended normalized results receive new Borealis rows. Those rows use the same result helper and non-destructive inline-detail behavior as first-page rows.

The continuation UI also preserves the core state-machine behavior already covered by host tests:

- continuation requests do not resend the original query;
- a continuation belongs to the active Search generation;
- new query/generation invalidates stale continuation completion;
- repeated continuation tokens are rejected;
- normalized duplicate identities are not appended twice;
- continuation failure keeps existing results and selection intact;
- continuation failure is explicitly retryable through `Retry Load more`;
- zero-result terminal continuation becomes end-of-results and hides `Load more`;
- the same scoped parser and Shorts/ad/promoted/shopping firewall applies to continuation pages.

If `Load more` is the focused View when a terminal page removes it, the UI first moves focus to a stable existing result. B/sidebar handling is otherwise unchanged from the physically accepted lifecycle fix.

Physical hardware subsequently proved the continuation request/data path at `959a46e7c1d5de1743cdf3b11a48b1b227ab0b4e`: additional videos fetched and appended without a crash. That overall UI checkpoint was rejected because the selector could become invisible after append and Up from the sibling `Load more` control could enter the nested results box at its first result. Pinned Borealis confirms the mechanism: sibling traversal into a Box resolves through that Box's default focus, while ScrollingFrame recenters on focus gain rather than merely on a focused View moving after layout growth. Fix `d432ef8c6eee8e1b77ecd8a96b78832c43bb8a5b` preserves append-only result Views, adds an explicit Up route from `Load more` to the current last result, queues focus by stable identity to the first newly appended result for the next post-layout frame, and lets A on an already-selected result call `clear_selection()` to collapse its inline detail. The continuation UI is **not hardware accepted** until that corrected focus sequence passes on the real Switch.

### Error states

The Switch UI consumes the existing UI-safe Search error taxonomy rather than raw request/response data. It distinguishes policy/network/HTTP/parser/unsupported/continuation conditions through safe user-facing text.

The UI does not expose request JSON, response JSON, continuation tokens, cookies, authorization material, visitor IDs or session identifiers.

## Live guest Home first-page implementation

The first Home slice reuses the accepted app-lifetime `LibnxHttpClient`, memory-only guest session and exact `www.youtube.com:443` network policy. Startup still performs no hidden Home request. After the user explicitly reaches `Guest ready`, Home exposes `Load Home` / `Refresh Home`, which sends the existing minimal `FEwhat_to_watch` browse request off the Borealis/UI thread.

The network-facing parser is scoped to the selected Home browse tab and recognized continuation action arrays. It does not recursively scan topbar/menu/command siblings. Within the selected Home container, only reviewed structural wrappers and leaf renderer families are opened; unreviewed, Premium/promo and command-only wrappers remain opaque even if they contain a video-shaped descendant. Recognized leaf renderers reuse the bounded Video / Channel / Playlist normalization and hard Shorts/reel, ads/promoted and shopping firewall already exercised by Search.

`HomeModel` owns normalized data, generation and selected identity independently from Borealis Views. TabFrame can therefore destroy and reconstruct the Home page without giving a worker stale UI pointers. The first Switch presentation is intentionally text-only: normalized thumbnail candidates may exist in memory but no remote image host is contacted. A toggles the same clean canonical ID/URL detail style proven by Search. Home continuation tokens are parsed as an architectural seam but the UI intentionally defers Home pagination until the first page passes hardware.

Home is **IMPLEMENTED / HOST-VALIDATED / PENDING PHYSICAL SWITCH TEST**. It is not yet called hardware accepted.

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

First-page live Search is **PHYSICALLY ACCEPTED** at checkpoint `a5fb638abd6e41d82490a460f41ea096b11d1ea5`. On the real Atmosphère Switch, the query `nintendo switch homebrew` returned 15 normalized live results, the UI reported that more results were available, controller navigation worked, and selecting a normal video produced its normalized video ID plus clean canonical `https://www.youtube.com/watch?v=...` URL.

The result-detail/sidebar lifecycle is also **PHYSICALLY ACCEPTED** using NRO head `d6910a232732b2bd9169abb11dfdf320cf35a34b` and UI fix checkpoint `2642ba7ff559879222a6cddc2b236afbd07c0012`. Hardware confirmed inline detail selection, persistent Search content after B, correct Search sidebar focus, and traversal through Home, Search, Subscriptions, Library and Settings without the previous blank-page or Home/Search-only trap.

The original pre-first-frame `std::abort (0xFFE)` root cause remains **UNKNOWN**. Do not retroactively attribute it to Search networking, `bad_alloc`, or the worker hardening without new evidence.

Search continuation / `Load more` is now **PHYSICALLY ACCEPTED** at `428497c90be6e768c607e45c68171827d74f0faf`, completing the Search hardware slice. The next activation gate is the explicit first-page guest Home candidate. It is implemented and host validated but is **NOT physically accepted** until the real Switch proves normal Home results, inline A/A-again detail, Home tab lifetime/sidebar stability, Search non-regression and normal exit/relaunch. Home continuation and remote thumbnails remain disabled for this gate; the exact `www.youtube.com:443` allowlist remains unchanged.

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
- First-page Search was subsequently exercised successfully on the physical Atmosphère Switch at `a5fb638abd6e41d82490a460f41ea096b11d1ea5`: the live query returned 15 results and a selected normal video showed its normalized ID and clean canonical URL. First-page live Search is therefore **PHYSICALLY ACCEPTED**. `Load more` remained disabled pending the later UI lifecycle gate.

## Inline-detail focus/page-lifecycle regression — 2026-09-13

Physical hardware rejected `36015cb2bf9ae98fc983674563e9df0935232123` as an overall UI checkpoint even though its inline detail rendering worked. Upper/middle/lower result selection stayed on Search and displayed the normalized ID plus clean canonical URL, but returning toward the sidebar could blank Search and leave only Home/Search reachable.

Source review of pinned Borealis `20e2d33b6c4ffce139ce304c503c04f5b94da920` identifies the unsafe interaction. `Box::removeView()` synchronously deletes result Views, while the inline implementation removed every result child and then called `Application::giveFocus()` on a replacement result. More importantly, TabFrame constructs a tab by calling its creator before attaching the returned page. When Search was re-entered from the sidebar with a selected identity, `create_page(Search)` rebuilt results and force-focused the selected result during the SidebarItem activation callback. That stole focus out of the sidebar before Down could continue to Subscriptions.

The Sections/B path also targeted the Sidebar container. Borealis resolves container focus via `getDefaultFocus()`, which selects the first sidebar item (Home); activating Home causes TabFrame to synchronously remove the Search page. The replacement resolves the actual active SidebarItem instead, so B from Search does not change the active tab or destroy the Search page.

The accepted fix removes selection-time list rebuilding entirely. Result buttons and metadata remain alive; each result owns a pre-created detail label whose `VISIBLE`/`GONE` state changes when selection changes. Full list rebuilding remains limited to genuinely new Search results or reconstructing a Search page after a real tab switch.

Hardware-tested NRO head `d6910a232732b2bd9169abb11dfdf320cf35a34b` passed the full inline-detail/sidebar traversal sequence and is now accepted for this UI lifecycle gate. Explicit continuation / `Load more` is implemented separately at `534f576a77aeb8f7226b2f3a3549be23c621e06a` and remains pending physical Switch acceptance. Guest bootstrap and first-page Search acceptance remain unchanged.