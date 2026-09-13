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
- Any future network stack (direct libnx SSL, curl, image loader, media resolver, auth, updater, SponsorBlock, DeArrow, etc.) must pass the same policy before DNS.

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

### Switch HTTPS investigation

A strict-verification curl transport was prototyped and builds successfully, but it is **not approved as the live/default path**.

Important findings:

- devkitPro `switch-curl` uses its custom libnx SSL backend and the console SSL service/trust infrastructure; a bundled mbedTLS CA path is not the correct default assumption.
- devkitPro issue #436 (opened 2026-08-13) reports a hard crash in the current curl/libnx certificate-info path during routine HTTPS, even when the application did not request certificate info.
- TizenTube NX issue #7 tracks this blocker.
- Curl automatic redirect following has been disabled.
- The curl prototype now applies the TizenTube NX outbound allowlist before curl can perform DNS resolution.
- The preferred M2 live path is now a small direct libnx SSL-service HTTP transport with peer-CA + hostname verification, avoiding the buggy curl certificate-extraction layer.

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
- Automatic curl redirects are disabled so redirects cannot bypass the host policy.
- Dedicated host tests verify Nintendo and redirect/authority tricks are blocked locally.

Network-safety checkpoint is on the current M2 branch and must remain green before live-network integration.

## Immediate next technical checkpoint

1. Finish the direct libnx SSL HTTP/1.1 transport behind the default-deny destination policy.
2. Support GET + POST, bounded response bodies and bounded redirects where each redirect target is revalidated before DNS.
3. Execute the first live guest `sw.js_data` HTTPS request off the Borealis UI thread without logging visitor/session data.
4. Parse live Search/Home results into renderer-neutral records.
5. Run every parsed record through the renderer firewall before creating UI cards.
6. Wire continuation paging after the first page is proven on real hardware.

No account login, playback, SponsorBlock or DeArrow is claimed at this checkpoint.

## Validation

- M1 is accepted on real Atmosphere hardware.
- Host CMake tests are green through the guest-session parser checkpoint and the new network-policy suite is green on the current branch.
- devkitA64 Switch build remains the required native gate for every networking change.
- Live YouTube networking has **not** yet been wired into the UI or accepted on-device.

## Known high-risk areas

- **Console safety:** Nintendo network destinations must never be reachable through app-controlled networking. 90DNS is defense in depth, not the app's primary safeguard.
- YouTube response/session shapes are private implementation details and may change. Keep parsing isolated, fail closed on unknown renderers, and cover known shapes with fixtures.
- Switch TLS must stay certificate-verified. Do not work around transport bugs by disabling peer or hostname verification.
- Current devkitPro switch-curl has an open certificate-info hard-crash report; do not make it the live/default browsing transport unless that risk is removed and retested.
- Account authentication remains a future risk. The product requirement is easy console-style login without cookie-file import. Before account-dependent UI, validate a sustainable TV/device authorization flow suitable for redistribution and avoid embedding third-party private credentials.
- Playback is a separate M3 risk and is not implied by successful guest browsing.
