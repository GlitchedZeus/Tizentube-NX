# TizenTube NX profiles and post-v1 YouTube linking/import

## Product decision

TizenTube NX does not need a Google/YouTube account to provide an account-like experience.

For v1, profiles are **local-first TizenTube profiles** created directly on the Switch. A user can create a profile, choose a display name and avatar, and then build their own TizenTube library without Google authentication.

This remains the primary account model for v1.

## V1 — local TizenTube profiles

A local TizenTube profile should be able to own, at minimum:

- display name and avatar;
- local channel follows/subscriptions;
- local playlists;
- Watch Later;
- local likes/favorites;
- watch history;
- playback resume positions;
- search history if enabled by the user;
- per-profile playback, audio, caption, privacy and UI preferences;
- future local recommendation signals derived from that profile's activity.

Actions such as Follow/Subscribe, Like, Watch Later and playlist edits modify the local TizenTube profile only unless a future write-back feature is separately designed and explicitly approved.

Multiple local TizenTube profiles may be supported. A later UX may optionally associate a TizenTube profile with a Nintendo Switch user, but the TizenTube data model must remain independent of Nintendo and Google accounts.

## V1 security boundary

Google authentication is not part of the v1 profile architecture.

For v1, TizenTube NX must never request, accept, transmit, persist or log:

- Google passwords;
- Google OAuth access or refresh tokens;
- authenticated YouTube cookies;
- browser-session cookies;
- exported cookie files;
- equivalent Google account/session credentials.

This keeps v1 local-first and reduces both account-takeover risk and maintenance caused by Google authentication changes.

## Post-v1 — optional real YouTube account linking

After v1, TizenTube NX may optionally link a real YouTube account using Google's official limited-input / device-code OAuth flow.

The preferred UX is console-style:

1. User chooses `Link YouTube` from an existing local TizenTube profile.
2. The Switch displays a QR code and a human-readable device code / verification instruction.
3. The user scans the QR or enters the code on a phone.
4. The phone opens Google's real sign-in and authorization UI.
5. Passwords, passkeys, 2FA and account challenges are handled by Google, not TizenTube NX.
6. The user grants the minimum YouTube permission required.
7. TizenTube NX receives OAuth authorization material but never the Google password or browser session cookies.
8. Approved YouTube library data may be imported/synced into the existing local TizenTube profile.

The Switch must never show a fake Google login form or ask the user to type a Google password into TizenTube NX.

See `docs/OAUTH_ACCOUNT_LINKING.md` for the security and token-lifecycle contract.

## Linked-account direction

The initial linked-account feature should prefer one-way, read-only synchronization:

`YouTube account -> TizenTube profile`

This may import or refresh approved library data such as subscriptions, playlists, liked/favorite library data where supported, and account/channel metadata required for import.

Do not request write-capable Google/YouTube scopes merely for convenience. Any future YouTube write-back feature requires its own security review and explicit user consent.

## Persistent authorization

Post-v1, users may be offered two modes:

### Keep me connected

- retain only the long-lived OAuth authorization needed to refresh short-lived access;
- store it as a sensitive secret using the strongest practical Switch-homebrew storage model;
- never store it as obvious plaintext on the SD card;
- never log/export/back up the token accidentally;
- silently refresh short-lived access tokens when possible.

### Do not remember authorization

- do not retain long-lived Google authorization;
- require the phone/device flow again for a future sync.

If authorization expires, is revoked or becomes invalid, the local TizenTube profile must continue working and should show `Reconnect YouTube` rather than losing local data.

## Local-first merge model

Linking YouTube augments a TizenTube profile; it does not replace it.

Imported/synced data should merge without duplicate stable IDs and preserve local-only data.

Losing or revoking Google authorization must never delete:

- local follows;
- local playlists;
- Watch Later;
- favorites/likes;
- watch history;
- resume positions;
- settings.

`Unlink YouTube` and `Delete TizenTube profile` must remain separate actions.

## No-auth/public import fallback

A later migration feature may also support importing public YouTube profile/channel data and user-supplied public/unlisted playlist links without account authentication.

That path can remain useful for users who do not want OAuth linking.

Unlisted playlist URLs must be treated as secrets and redacted from diagnostics.

## Security goals

Whether using OAuth linking or a public-data import helper:

- never collect a Google password;
- never collect passkeys/2FA recovery codes;
- never import browser cookies;
- never accept exported cookie files;
- use least-privilege scopes;
- prefer read-only YouTube permissions;
- redact tokens and authorization headers everywhere;
- keep short-lived access tokens in memory where practical;
- protect any retained refresh token as a high-value secret;
- provide obvious unlink/reconnect behavior;
- preserve local profile data when authorization fails;
- apply the project outbound network policy before every new auth/API destination is resolved;
- do not add auth endpoints to the live allowlist until a dedicated implementation/security milestone reviews them.

A modded Switch should be treated as a trusted-user environment, not as a guarantee that every homebrew/sysmodule on the device is trustworthy.

## Release planning

### V1

- local TizenTube profile creation;
- local follows/subscriptions;
- playlists;
- Watch Later;
- favorites/likes;
- history/resume;
- profile settings;
- no Google account login required.

### Post-v1

- optional QR + device-code Google OAuth linking;
- phone opens Google's official sign-in/consent UI;
- least-privilege read-only sync into an existing TizenTube profile;
- optional `Keep me connected` persistent authorization;
- secure token storage review before persistence ships;
- reconnect when authorization expires/revokes;
- unlink/revoke support;
- optional public/no-auth profile and playlist migration fallback;
- duplicate-safe merge/re-import/sync.

Post-v1 account linking must never turn TizenTube NX into a collector of Google passwords, browser cookies or unrelated Google credentials.