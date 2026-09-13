# Native network service lifetime

This note records the real-Switch M2 guest-bootstrap failure observed after the native libnx transport first reached hardware.

## Hardware observation

Pressing `Test YouTube guest connection` produced:

`socketInitializeDefault failed: 0x00000f59`

The result decodes to libnx `LibnxError_AlreadyInitialized`. This failure occurred before DNS, TCP, TLS, HTTP, or a YouTube request.

## Proven source of the pre-existing socket environment

TizenTube NX pins Borealis commit `20e2d33b6c4ffce139ce304c503c04f5b94da920`. Its Switch `switch_wrapper.c` supplies `userAppInit()` and calls `socketInitializeDefault()` before `main()`. The same wrapper later calls `socketExit()` from `userAppExit()`.

Borealis also calls `nxlinkStdio()` after socket initialization. nxlink therefore uses the already-created environment but is not the initializer responsible for the first socket setup.

TizenTube NX must not remove this legitimate framework initialization simply to make another `socketInitializeDefault()` call return success.

## libnx socket ownership semantics

libnx socket initialization is not guarded by the service reference-count helper. `socketInitializeDefault()` ultimately checks whether the `soc:` devoptab is already present. If it is, it returns:

`MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)`

`socketExit()` removes the `soc:` devoptab and exits BSD. Therefore a caller that receives `AlreadyInitialized` did not acquire a matching cleanup obligation and must not call `socketExit()` for that borrowed environment.

TizenTube NX classifies socket initialization as:

- success: usable, owned by this component, matching `socketExit()` required;
- exact libnx `AlreadyInitialized`: usable, borrowed, no `socketExit()` by this component;
- any other error: unusable, initialization fails.

No raw `0xF59` comparison is used in production code; the Switch client constructs the exact result with `MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)`.

## libnx SSL ownership semantics

`sslInitialize(3)` is different. libnx implements it through `NX_GENERATE_SERVICE_GUARD_PARAMS`, whose `ServiceGuard` increments a process-local reference count on every successful public initialize call and decrements it on `sslExit()`.

Consequently:

- a successful TizenTube NX `sslInitialize(3)` acquires one matching `sslExit()` obligation, even if another component already has SSL initialized;
- any non-zero SSL initialization result remains a failure for this caller;
- TizenTube NX does not reinterpret a hypothetical SSL `AlreadyInitialized` error as a borrowed success under the current libnx ServiceGuard implementation.

Socket and SSL ownership are tracked independently.

## Application / worker lifetime

`LibnxHttpClient` is now owned by `ShellActivity` instead of being created inside every guest-test worker. This gives the native HTTPS service state activity/application lifetime while request workers borrow the stable client.

Shutdown order is deliberate:

1. `ShellActivity` joins any active network worker;
2. its `LibnxHttpClient` destructor releases the SSL reference acquired by TizenTube NX;
3. it calls `socketExit()` only if TizenTube NX itself initialized sockets;
4. when sockets were already present from Borealis, they remain untouched for Borealis `userAppExit()` to release.

No hidden network request is introduced by constructing the client. The only live M2 request remains the explicit Home `Test YouTube guest connection` action.

## Safe hardware diagnostics

The guest probe keeps coarse, non-sensitive stages:

- `Socket init failed`
- `SSL init failed`
- `Network policy blocked`
- `TCP failed`
- `TLS failed`
- `HTTP failed`
- `Bootstrap rejected`
- `Guest ready`

The detail line may include service state such as `Sockets: existing | SSL: ready`, but never credentials, visitor/session IDs, cookies, authorization values, continuation tokens, or response bodies.

## Unchanged safety gate

The exact M2 outbound allowlist remains `www.youtube.com:443`. Nintendo endpoints remain denied before DNS. `switch-curl` remains excluded. Live Search remains disabled until the explicit guest bootstrap reaches `Guest ready` on physical hardware.
