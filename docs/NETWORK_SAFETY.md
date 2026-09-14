# TizenTube NX network safety

## Hard invariant: never contact Nintendo network endpoints

TizenTube NX is intended to run on modded Nintendo Switch consoles. An accidental connection to Nintendo-owned network endpoints is unacceptable because it may expose the console to account/device enforcement risk.

This is a **release-blocking security invariant**, not a preference and not an optional setting.

## Defense model

### 1. Default-deny outbound networking

TizenTube NX must reject every outbound hostname unless it is explicitly allowlisted in source code for a reviewed feature.

For M2 guest browsing the only application-layer destination currently allowlisted is:

- `www.youtube.com:443`

No wildcard such as `*.youtube.com` is permitted. Future hosts needed for thumbnails, playback, SponsorBlock, DeArrow, authentication, updates, or any other feature must be added deliberately with tests and review.

### 2. Policy runs before DNS

The allowlist check must happen **before**:

1. DNS resolution (`getaddrinfo` or equivalent)
2. socket connection
3. TLS handshake
4. HTTP request transmission

A blocked Nintendo URL must therefore produce no DNS lookup initiated by TizenTube NX and no connection attempt.

### 3. Explicit Nintendo hard-deny

Known Nintendo-owned domain families are explicitly denied in addition to the default-deny policy, including:

- `nintendo.com` and all subdomains
- `nintendo.net` and all subdomains
- `nintendo.co.jp` and all subdomains
- `nintendowifi.net` and all subdomains
- `nintendo-europe.com` and all subdomains

The explicit deny exists as defense in depth for future allowlist changes.

### 4. Redirects never bypass policy

HTTP libraries must not autonomously follow redirects to arbitrary hosts.

Every redirect target must be parsed and passed through the same outbound policy **before** another DNS lookup or connection. If the target is not explicitly allowlisted, the redirect fails locally.

### 5. 90DNS is defense in depth only

The user may run 90DNS or equivalent Nintendo-domain blocking. TizenTube NX must never depend on that configuration for its own safety. The app's own destination policy is mandatory even when external DNS blocking is present.

### 6. Local Horizon/libnx services are not Nintendo network traffic

Using local console services such as BSD sockets, `ssl`, `nifm`, filesystem services, HID, or other libnx/Horizon IPC does not itself constitute an outbound Nintendo-server connection.

Network safety is enforced on the remote destination before DNS/connect operations.

## Required tests

The host suite must continue proving at minimum that:

- exact approved YouTube HTTPS destinations pass;
- plain HTTP fails;
- alternate ports fail;
- IP-literal destinations fail;
- non-allowlisted hosts fail;
- Nintendo roots and deep subdomains fail;
- deceptive names such as `www.youtube.com.nintendo.net` fail;
- URL userinfo tricks involving Nintendo hosts fail;
- unreviewed redirector hosts fail.

Any future networking implementation (curl, direct libnx SSL, media stack, image loader, updater, authentication client, etc.) must call the same policy before DNS.

## Release rule

**If a code path can reach an outbound destination without passing the default-deny policy first, that build is not releaseable.**
