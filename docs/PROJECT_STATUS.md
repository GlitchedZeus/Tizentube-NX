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

## M2 implemented so far

Branch: `feature/m2-guest-browsing`.

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
- Normal InnerTube requests do **not** embed a fixed third-party/private API key in source or URLs.

Checkpoint: `2edf3ab18c798e1e69e548e1b2623fd079a2c254`.
Host and devkitA64 Switch CI both pass.

### Native Switch HTTPS transport

- Blocking libcurl transport implementing the shared HTTP client interface.
- Switch sockets and libcurl lifecycle are owned by the transport.
- GET and POST are supported with bounded connect/request timeouts.
- Redirects are bounded and restricted to HTTPS.
- Plain HTTP is rejected.
- TLS peer and hostname verification are always enabled.
- An explicit CA bundle is required; there is no insecure verification-disabled fallback.
- Static Switch link order includes curl, mbedTLS and zlib dependencies.

Checkpoint: `fbf4f792aefb59c0fcd322c9d62a55b062e0f84f`.
Host CI and the devkitA64 NRO build both pass.

## Immediate next technical checkpoint

1. Add a pinned trusted CA bundle to RomFS and document how it is refreshed.
2. Parse YouTube `sw.js_data` into the guest session model without logging visitor/session data.
3. Execute the first live guest HTTPS request off the Borealis UI thread.
4. Parse live Search/Home results into renderer-neutral records.
5. Run every parsed record through the renderer firewall before creating UI cards.
6. Wire continuation paging after the first page is proven on real hardware.

No account login, playback, SponsorBlock or DeArrow is claimed at this checkpoint.

## Validation

- Host CMake tests are green through `fbf4f792aefb59c0fcd322c9d62a55b062e0f84f`.
- devkitA64 Switch build is green through `fbf4f792aefb59c0fcd322c9d62a55b062e0f84f`.
- M1 is accepted on real Atmosphere hardware.
- The M2 networking code has compiled into an NRO, but live YouTube networking has **not** yet been wired into the UI or accepted on-device.

## Known high-risk areas

- YouTube response/session shapes are private implementation details and may change. Keep parsing isolated, fail closed on unknown renderers, and cover known shapes with fixtures.
- Switch TLS must stay certificate-verified. Do not work around CA problems by disabling verification.
- Account authentication remains a future risk. The product requirement is easy console-style login without cookie-file import. Before account-dependent UI, validate a sustainable TV/device authorization flow suitable for redistribution and avoid embedding third-party private credentials.
- Playback is a separate M3 risk and is not implied by successful guest browsing.
