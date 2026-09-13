# Project status

## Current phase

M2 — YouTube guest browsing / pre-alpha.

## Accepted baseline

M1 is **device accepted** on a real Atmosphere Switch.

Hardware acceptance reported on 2026-09-12:

- The latest M1 `tizentube_nx.nro` boots and works on-device.
- The native Borealis shell is the accepted UI baseline.
- Home, Search, Subscriptions, Library and Settings remain the only root sections.
- Shorts are not a root route and the hard no-Shorts invariant remains covered by host tests.
- Canonical clean YouTube share-link behavior remains covered by host tests.
- M1 was squash-merged to `main` at `4505fabdda70b22227257848a8d8cbb110bbd81e`.

The last pre-merge M1 native artifact was built from
`3d869ebd88e57da4b2391401d78e16464ea71a06`:

- NRO: `tizentube_nx.nro`, 1,864,638 bytes
- SHA-256: `9c31ab733f03d33e6a202c574f4bf8c5e6d07ff0707309fb35a74e0856e10d70`

## Hard console-network safety invariant

**TizenTube NX must never intentionally resolve or connect to Nintendo network endpoints.**

This is release-blocking and does not rely on 90DNS:

- Outbound networking is default-deny.
- M2 currently allowlists only exact `www.youtube.com:443`.
- The policy runs before DNS lookup, socket connect, TLS or HTTP transmission.
- Known Nintendo domain families are explicitly hard-denied as defense in depth.
- Automatic cross-host redirects are forbidden; every redirect must be revalidated before another DNS lookup.
- IP-literal destinations, userinfo authority tricks, non-HTTPS destinations and alternate ports are denied.
- Host tests cover Nintendo roots/deep subdomains and deceptive authority forms.
- Any future network stack (direct libnx SSL, image loader, media resolver, auth, updater, SponsorBlock, DeArrow, etc.) must pass the same policy before DNS.

See `docs/NETWORK_SAFETY.md`.

## M2 implemented so far

Branch: `feature/m2-guest-browsing`.
Draft PR: #6 — `M2: guest browsing networking foundation`.

### Guest content boundary

- Renderer-neutral guest request/page model for Home, Search, Channel and Playlist surfaces.
- Opaque continuation tokens for pagination; tokens are never rewritten or interpreted.
- A renderer firewall runs before UI presentation.
- Known Shorts/reel renderers are rejected even if they claim to be ordinary videos.
- Promoted/ad renderers are rejected before UI presentation.
- Shopping/product renderers are rejected by the default policy.
- Unsupported/unknown leaf renderers are dropped instead of being guessed into a visible card.
- Shorts and promoted content are hard invariants: future settings cannot re-enable them accidentally.

Checkpoint: `190797bb96991d417f9dc5811cfa900b983bb4b4`.
Host and devkitA64 Switch CI both pass.

### Guest request construction

- Transport-neutral HTTP request/response/client contract.
- YouTube guest session model with client version, visitor data, locale, region and timezone.
- Session bootstrap request for YouTube `sw.js_data`.
- Search, Browse and Home InnerTube request builders.
- Home uses the standard `FEwhat_to_watch` browse ID.
- Continuation requests preserve the opaque continuation token and do not resend the original query/browse ID.
- JSON string escaping is covered by host tests.
- Normal InnerTube request builders do **not** embed a fixed third-party/private API key in source or URLs.

Checkpoint: `2edf3ab18c798e1e69e548e1b2623fd079a2c254`.
Host and devkitA64 Switch CI both pass.

### Guest session bootstrap parser

- Bounded parser for YouTube's `sw.js_data` JSPB/XSSI response.
- Requires the expected XSSI prefix and current guest bootstrap array shape; unexpected shapes fail closed.
- Extracts only the data required for anonymous guest requests: WEB client version, visitor data, locale/region and timezone.
- Caller locale/timezone/user-agent overrides are supported without logging or persisting visitor/session values.
- JSON strings are decoded safely, including escapes and Unicode surrogate pairs.
- Input is capped at 4 MiB and nesting at 128 levels.
- Malformed, truncated, oversized and wrong-type fixtures are covered by host tests.

Checkpoint: `ba3d807ef4e417c4fb10fb7b6cb3569a1d2d5185`.
Host CI and the devkitA64 NRO build both pass.

### Outbound network policy

- Default deny; no wildcard hosts.
- Exact M2 application-layer allowlist: `www.youtube.com:443` only.
- Explicit Nintendo-family hard deny independent of 90DNS.
- URL validation occurs before DNS/connect.
- HTTP, alternate ports, IP literals, userinfo and malformed DNS names are rejected.
- Every native HTTP redirect is revalidated immediately and again before the next DNS lookup.
- Dedicated host tests verify Nintendo and redirect/authority tricks are blocked locally.

Network-safety policy remains release-blocking.

### Strict HTTP/1.1 codec

- Host-tested HTTPS URL parser restricted to port 443.
- GET/POST request serializer owns `Host`, `Connection`, `Accept-Encoding`, framing and POST `Content-Length`.
- Caller CRLF/control-character header injection is rejected.
- Response parser accepts bounded HTTP/1.0/1.1 responses, fixed-length, chunked and connection-close framing.
- Ambiguous framing (duplicate `Content-Length`, duplicate `Transfer-Encoding`, or both TE + CL) fails closed.
- Truncation, trailing bytes after declared framing, invalid chunks, oversized bodies and body-forbidden status responses fail closed.
- Redirect `Location` is surfaced but never followed by the codec itself.

Host CI passes with the HTTP/1.1 suite wired into CMake.

### Native Switch HTTPS transport

The approved M2 transport is now a direct libnx implementation using BSD sockets plus the console's local Horizon SSL service.

Implemented and compile-verified:

- Two application policy gates exist before remote contact: `perform()` validates the full URL before networking, and the only DNS helper checks the allowlisted hostname immediately before `getaddrinfo()`.
- IPv4 TCP connect is bounded with a non-blocking connect/poll timeout and socket send/receive timeouts.
- TLS is limited to TLS 1.2, plus TLS 1.3 on HOS 11.0.0+.
- Peer-CA, hostname and certificate-date verification are explicitly enabled.
- Hostname/SNI is set before handshake.
- The libnx socket-to-SSL descriptor wrapper is used, with correct returned-descriptor ownership/close order.
- Requests and raw/decoded responses are bounded.
- GET redirects are bounded to three hops and each target is policy-validated before another DNS lookup.
- POST redirects fail closed for the first milestone rather than changing method semantics implicitly.
- The current `switch-curl` prototype remains source reference only and is filtered out of the Switch build.
- Curl/mbedTLS/zlib link dependencies were removed from the NRO build path; the native transport links through libnx only.

Native transport checkpoint: `9042b2ba376c10d5f51ada8487f4799187b625af`.
Host tests and devkitA64 Switch build both pass at this checkpoint.

### User-triggered hardware probe

A first real-network path is now wired into the Borealis shell for hardware validation without adding hidden startup traffic:

- Home exposes `Test YouTube guest connection`.
- No YouTube request is made automatically when the app boots.
- The action starts a worker thread; DNS, TCP, TLS, HTTP and bootstrap parsing stay off the Borealis UI loop.
- The worker uses the native libnx transport and therefore the same pre-DNS exact-host allowlist.
- The probe GETs only the existing `https://www.youtube.com/sw.js_data` bootstrap endpoint.
- Successful guest session data is held in memory only.
- Visitor/session values are never written to the UI status text, logs or SD storage.
- Worker results are transferred through a mutex-protected model; the worker never mutates Borealis views directly.
- The worker is joined during normal shutdown so socket/SSL services are torn down cleanly.
- Search remains intentionally network-disabled until the bootstrap itself succeeds on real hardware.

Hardware-probe compile checkpoint: `bdeb76fe5a896127ca0c4a304a0bb3794a064b0a`.
Host tests and devkitA64 Switch build both pass at this checkpoint.

### Scoped Search parsing and executor

The offline/host-tested Search path is now hardened before any live Search UI activation:

- The reusable low-level renderer parser still supports legacy `videoRenderer`, modern `lockupViewModel`, channels, playlists, legacy continuation renderers and modern continuation-item views.
- A separate `parse_scoped_search_response()` boundary now validates the full JSON document without running renderer extraction over unrelated root data.
- Only `contents.twoColumnSearchResultsRenderer.primaryContents` and direct `appendContinuationItemsAction` / `reloadContinuationItemsCommand` continuation payloads under the recognized `onResponseReceived*` arrays can reach renderer traversal.
- Continuation discovery is deliberately non-recursive outside those direct action shapes, so renderer-looking command/menu/metadata siblings cannot become Search cards.
- Renderer firewalling still runs inside accepted Search payloads: Shorts/reels, disguised reel endpoints, promoted/ad subtrees, shopping/product subtrees and unknown renderers remain blocked.
- Hostile fixtures cover topbar, header, sidebar, metadata, continuation-command siblings and nested fake append actions outside the accepted boundary.
- The transport-neutral Search executor now calls the scoped parser.
- Fake-HTTP tests still prove first-page Search sends the query, continuation Search sends only the opaque continuation token, and the original query is not resent.
- Live Search remains disabled in the Switch UI.

Scoped Search code/test checkpoint: `ec68df8f06c5801c8b6d58d431bf70bcfc7d0c15`.
Host configure/build/full `ctest` and the devkitA64 NRO build all pass at this checkpoint.
The corresponding CI NRO artifact was successfully produced.

This is still a **compile/integration checkpoint**. A live YouTube HTTPS request has not yet been declared successful on real Switch hardware.

## Immediate next technical checkpoint

1. Install/test the current PR #6 CI artifact on the real Atmosphere Switch.
2. On Home, explicitly choose `Test YouTube guest connection` and verify the UI remains responsive while the worker runs.
3. Record only the resulting non-sensitive status (`Guest ready`, transport error code, or bootstrap-shape error); do not capture visitor/session values.
4. If the probe succeeds, treat direct libnx HTTPS + guest bootstrap as hardware accepted.
5. Only then connect the already host-tested Search executor to the Switch worker path and enable the first live Search POST.
6. Keep all Search results behind the scoped response boundary and renderer firewall before UI creation.
7. Wire Home and continuation paging only after first-page live Search is proven on real hardware.

No account login, playback, SponsorBlock or DeArrow is claimed at this checkpoint. Shorts remain absent by design.

## Validation

- M1 is accepted on real Atmosphere hardware.
- Host CMake configure/build/full test suite is green through the scoped Search boundary/executor checkpoint.
- devkitA64 successfully compiles and links the direct libnx SSL transport and scoped Search code into the NRO.
- The off-thread, user-triggered bootstrap probe remains the only live M2 YouTube request path in the Switch UI.
- `switch-curl` is not linked into the NRO; `Makefile.switch` filters the prototype and links native libnx only.
- Exact M2 outbound allowlist remains `www.youtube.com:443`.
- Live Search POST is **not** enabled in the UI.
- Live YouTube networking has **not** yet been accepted on-device.

## Known high-risk areas

- **Console safety:** Nintendo network destinations must never be reachable through app-controlled networking. 90DNS is defense in depth, not the app's primary safeguard.
- YouTube response/session shapes are private implementation details and may change. Keep parsing isolated, scope Search traversal narrowly, fail closed on unknown renderers, and cover known shapes with fixtures.
- Switch TLS must stay certificate-verified. Do not work around transport bugs by disabling peer, hostname or date verification.
- Current devkitPro switch-curl has an open certificate-info hard-crash report; it remains excluded from the NRO live path unless that risk is removed and retested.
- The first real-hardware libnx SSL request may expose service/timeout/firmware edge cases that CI cannot simulate; treat hardware validation as mandatory before declaring the transport accepted.
- Account authentication remains a future risk. The product requirement is easy console-style login without cookie-file import. Before account-dependent UI, validate a sustainable TV/device authorization flow suitable for redistribution and avoid embedding third-party private credentials.
- Playback is a separate M3 risk and is not implied by successful guest browsing.
