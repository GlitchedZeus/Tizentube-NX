# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

Branch: `feature/m2-guest-browsing`  
Draft PR: #6 — `M2: guest browsing networking foundation`

Live Search remains disabled. The only live M2 action is the explicit Home `Test YouTube guest connection` probe.

## Real-hardware guest-bootstrap checkpoint

The native NRO boots successfully on the physical Atmosphere Switch.

The first explicit guest-connection test reached native service initialization and stopped before any DNS/TCP/TLS/HTTP/YouTube traffic with:

`socketInitializeDefault failed: 0x00000f59`

The UI reported `Network init failed` and the footer reported `M2 guest | Network init failed`.

`0x00000f59` is libnx `LibnxError_AlreadyInitialized`.

This result is specifically **not** evidence of DNS, TCP, TLS, HTTP, YouTube bootstrap, or outbound-policy failure; none of those stages had run yet.

## Proven socket initializer

The repository pins Borealis commit `20e2d33b6c4ffce139ce304c503c04f5b94da920`.

That pinned Borealis Switch wrapper provides `userAppInit()`, which runs before `main()` and calls `socketInitializeDefault()`. It later calls `socketExit()` from its matching `userAppExit()`. `nxlinkStdio()` is invoked after the socket initialization and uses that environment.

Therefore the pre-existing socket runtime is framework-owned. TizenTube NX must borrow it and must not tear it down.

See `docs/NATIVE_NETWORK_LIFETIME.md` for the ownership analysis and libnx source semantics.

## Native network lifetime fix

`LibnxHttpClient` now distinguishes cleanup ownership explicitly.

### BSD sockets

- `socketInitializeDefault()` success: socket environment is usable and TizenTube NX owns the matching `socketExit()`.
- exact `MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)`: socket environment is usable but borrowed; TizenTube NX does **not** call `socketExit()`.
- any other initialization error: fatal for this native client and preserved as an initialization failure.

The production code does not compare against hardcoded `0xF59`.

### SSL service

`sslInitialize(3)` uses libnx `ServiceGuard` reference-count semantics. A successful public `sslInitialize()` call acquires one matching `sslExit()` obligation even when another caller already holds an SSL reference.

TizenTube NX therefore tracks SSL separately from sockets:

- successful SSL initialization: usable and one matching `sslExit()` is required;
- any non-zero SSL initialization result: failure for this caller;
- a hypothetical SSL `AlreadyInitialized` error is not reinterpreted as a borrowed success under the current ServiceGuard implementation.

### Application / worker lifetime

The native HTTPS client is now owned by `main()` for application lifetime. `ShellActivity` and the guest-test worker borrow it.

The shutdown order is:

1. join the request worker;
2. leave `main()` and destroy TizenTube NX's HTTPS client;
3. release the SSL reference acquired by TizenTube NX;
4. call `socketExit()` only if TizenTube NX actually initialized sockets itself;
5. for the normal Borealis path, leave the borrowed socket environment intact for Borealis `userAppExit()`.

Repeated guest-test button presses no longer create service init/exit cycles.

## Hardware diagnostic stages

The next diagnostic NRO distinguishes safe coarse stages without exposing session material:

- `Socket init failed`
- `SSL init failed`
- `Network policy blocked`
- `TCP failed`
- `TLS failed`
- `HTTP failed`
- `Bootstrap rejected`
- `Guest ready`

Diagnostic detail may show service state such as `Sockets: existing | SSL: ready`. Credentials, cookies, authorization data, visitor/session IDs, continuation tokens and response bodies are not displayed.

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

Morphe remains a first-class future feature reference through `AGENTS.md` and `docs/MORPHE_REFERENCE.md`; those reference commits are preserved. Morphe does not weaken the project-wide no-Shorts invariant.

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

No new host was authorized by the socket ownership fix.

## Current activation gate

The socket `AlreadyInitialized` condition is now handled as a borrowed, usable environment and cannot cause TizenTube NX to call `socketExit()` on Borealis-owned state.

This is **not yet real-hardware networking acceptance**. A new physical-Switch test is required.

Next test:

`Test YouTube guest connection`

Expected progress: the probe must no longer stop at `socketInitializeDefault` / `LibnxError_AlreadyInitialized`. Report the next exact connection status and detail. The next stage may be SSL, TCP, TLS, HTTP, bootstrap parsing, or `Guest ready`; CI must not guess which hardware stage comes next.

Live Search must remain disabled until the explicit guest bootstrap reaches an accepted `Guest ready` result on hardware.
