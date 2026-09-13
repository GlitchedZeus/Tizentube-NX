# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

Branch: `feature/m2-guest-browsing`  
Draft PR: #6 — `M2: guest browsing networking foundation`

The real-Switch guest bootstrap gate is now accepted. The next activation target is live Search on physical hardware.

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

Therefore the physical hardware has now verified the complete guest bootstrap path through:

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

This does **not** yet mean live Search is hardware-accepted; Search activation is the next M2 gate.

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

The native HTTPS client is owned by `main()` for application lifetime. `ShellActivity` and explicit request workers borrow it. Repeated guest-test button presses do not create service init/exit cycles.

See `docs/NATIVE_NETWORK_LIFETIME.md` for the ownership analysis.

## Search / normalized-model state

The offline M2 Search work remains intact:

- scoped Search response boundary;
- hard Shorts/reel firewall;
- ads/promoted/shopping firewall;
- legacy and modern renderer normalization;
- bounded video/channel/playlist metadata;
- deterministic thumbnail candidates;
- canonical clean sharing;
- query-owned continuation flow;
- request/privacy hardening;
- renderer-independent presentation helpers;
- offline `SearchModel` with stale-generation and retry/end-of-results state;
- UI-safe Search error taxonomy.

The physical guest-bootstrap prerequisite for activating Search is now satisfied.

The next work should wire the existing Search stack into the Switch UI, keep the request off the UI thread, render normalized results, support continuation/load-more, and then produce a new NRO for physical-Switch Search acceptance.

Remote thumbnail downloads must not silently widen the outbound allowlist. If Search results contain image hosts other than `www.youtube.com`, keep placeholders/text-only cards until those hosts are separately reviewed and approved.

## V1 profile/account direction

V1 will use **local-first TizenTube profiles**, not Google/YouTube account authentication.

Users should be able to create a TizenTube profile directly on Switch and build local follows/subscriptions, playlists, Watch Later, favorites/likes, history/resume and settings.

TizenTube NX must not request or store Google passwords, OAuth tokens, authenticated cookies or equivalent Google account credentials.

A post-v1 optional YouTube→TizenTube migration feature is planned separately. It should use a Switch-displayed QR code with a short-code fallback to pair a phone to an ephemeral import session and copy only safely accessible public data plus user-supplied public/unlisted playlists into an existing local TizenTube profile.

See `docs/PROFILES_AND_IMPORT.md`.

## Feature-reference policy

Morphe remains a first-class future feature reference through `AGENTS.md` and `docs/MORPHE_REFERENCE.md`. Morphe does not weaken the project-wide no-Shorts invariant.

## Hard network safety invariant

**TizenTube NX must never intentionally resolve or connect to Nintendo network endpoints.**

This remains release-blocking and independent of 90DNS:

- outbound networking is default-deny;
- exact current M2 allowlist remains `www.youtube.com:443` only;
- policy evaluation happens before DNS/connect/TLS/HTTP;
- Nintendo domain families are hard-denied;
- HTTP, alternate ports, IP literals, userinfo and malformed authorities are rejected;
- every redirect is revalidated before another DNS lookup;
- `switch-curl` remains excluded from the NRO.

No host is authorized merely because YouTube returns a URL for it.

## Current activation gate

The real-Switch guest bootstrap is accepted as `Guest ready`.

The next gate is:

1. enable the already-tested live Search request path in the Switch UI;
2. render normalized Video/Channel/Playlist results without widening the allowlist for thumbnails;
3. retain the hard Shorts/ad/promoted/shopping firewall;
4. build a new NRO;
5. physically test first-page Search and continuation/load-more on the Switch.

Live Search must not be called hardware-accepted until that new build is tested on the physical Switch.
