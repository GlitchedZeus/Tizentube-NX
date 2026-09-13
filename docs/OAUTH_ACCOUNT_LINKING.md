# Post-v1 optional YouTube OAuth linking

## Product decision

TizenTube NX v1 remains local-first. A user can create and use a full TizenTube profile without a Google/YouTube account.

After v1, TizenTube NX may optionally support linking a real YouTube account through Google's official limited-input / device-code OAuth flow so the user's phone handles the real Google sign-in and consent screen.

The preferred UX is console-style:

1. User chooses `Link YouTube` from an existing local TizenTube profile.
2. The Switch displays a QR code and a human-readable device code / verification instruction.
3. The user scans the QR or enters the code on a phone.
4. The phone opens Google's real authorization UI.
5. Passwords, passkeys, 2FA and account challenges are handled by Google, not TizenTube NX.
6. The user explicitly grants the minimum YouTube permission required.
7. TizenTube NX receives only OAuth authorization material, never the Google password.
8. The local TizenTube profile may then import/sync approved YouTube library data.

The Switch must never render a fake Google login form or ask the user to type a Google password into TizenTube NX.

## Security boundary

The strongest security goal is that TizenTube NX never has access to the user's Google password, browser session, or authenticated YouTube cookies.

Forbidden account material:

- Google password;
- passkey/private-key material;
- 2FA recovery codes;
- browser-session cookies;
- authenticated YouTube cookies;
- exported browser cookie files;
- unrelated Google service credentials.

OAuth tokens are different: they are application credentials and may be used post-v1 only under the rules in this document.

## Least privilege

Account linking should request the narrowest practical YouTube scope, with a strong preference for read-only access.

The initial linked-account feature should be one-way synchronization:

`YouTube account -> TizenTube profile`

It should not require permission to modify the user's Google account or write back to YouTube.

Do not add write-capable scopes simply because they are convenient. Any future feature that writes to YouTube must receive a separate security/design review and explicit user-facing consent.

## Token model

Treat access and refresh tokens differently.

### Access token

- short-lived;
- used only when making approved YouTube account API calls;
- keep in memory where practical;
- never log it;
- never display it;
- do not include it in crash reports, diagnostics, backups, exports or URLs.

### Refresh token

A refresh token may be retained post-v1 if the user chooses `Keep me connected`.

It is a sensitive secret and must never be treated as harmless merely because the console runs Atmosphere.

The expected UX is:

- valid refresh token -> silently obtain a new short-lived access token when needed;
- expired/revoked/invalid refresh token -> keep the local TizenTube profile intact and show `Reconnect YouTube`;
- reconnect -> repeat the official phone/device OAuth flow;
- losing authorization must never delete local follows, playlists, favorites, history or settings.

Users should also be able to choose a non-persistent linking mode where no long-lived authorization is retained and future sync requires reconnecting.

## Persistent-token storage

Do not store a refresh token as plaintext in an obvious SD-card file such as `token.txt` or `refresh_token.json`.

Before persistent linking ships, investigate the strongest practical storage model available to a Switch homebrew NRO, including Horizon-managed application/save storage where appropriate.

If at-rest encryption is used, do not store the encryption key next to the ciphertext on the same SD card and call that secure. Prefer device-bound or user-derived key material where practical and reviewed.

The threat model must include:

- another malicious or compromised homebrew application;
- an over-privileged sysmodule;
- SD-card removal or copying;
- USB/PC file access;
- accidental profile backups;
- debug/crash logs;
- memory/error-path leakage.

A modded console is a trusted-user environment, not a magically trusted-software environment.

## Logging and diagnostics

Never log or display:

- access tokens;
- refresh tokens;
- authorization headers;
- device-code secrets after redemption;
- OAuth callback payloads containing secrets;
- authenticated cookies;
- raw account API responses containing sensitive account data.

Diagnostics should identify only safe coarse states such as:

- Not linked
- Waiting for phone authorization
- Linked
- Syncing
- Sync complete
- Authorization expired
- Authorization revoked
- Reconnect required

Any token-derived diagnostic must be fully redacted.

## Local-first behavior

A linked YouTube account augments a TizenTube profile; it does not replace it.

The local TizenTube profile remains the canonical app identity and should continue working if:

- the Switch is offline;
- Google authorization expires;
- the user revokes access;
- YouTube API access is temporarily unavailable;
- the user explicitly unlinks YouTube.

Imported/synced YouTube data becomes local TizenTube data according to documented merge rules.

## Initial post-v1 sync targets

Subject to the permissions and APIs actually available at implementation time, the first linked-account sync may include approved read-only data such as:

- subscriptions;
- playlists;
- liked/favorite library data where supported;
- channel/account metadata needed for import;
- other user-selected library data that can be fetched with the chosen least-privilege scope.

Do not promise a specific private YouTube surface until its API behavior has been verified during implementation.

## QR and device-code UX

The Switch may display both:

- a QR code for the phone; and
- a short human-readable code as a fallback.

The QR/code must lead to the official Google authorization path or a narrowly scoped TizenTube linking helper that immediately hands the user into Google's official authorization UI. TizenTube NX must not collect the user's Google password through an intermediary page.

The code flow must be rate-limited and bounded as required by the provider's device-flow contract.

## Unlink / revoke

Provide an obvious `Unlink YouTube` action.

On unlink:

- remove locally stored OAuth authorization material;
- attempt provider-side revocation when practical;
- keep the local TizenTube profile and already-imported local data unless the user separately requests deletion;
- clearly distinguish `unlink YouTube` from `delete TizenTube profile`.

## Release rule

OAuth linking is post-v1 work and must not delay the local-first v1 release.

Before shipping persistent linking, complete a dedicated security review covering scopes, token lifecycle, storage, redaction, revocation, reconnect behavior, network allowlisting, failure handling and local-data preservation.
