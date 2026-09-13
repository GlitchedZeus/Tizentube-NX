# TizenTube NX profiles and post-v1 YouTube import

## Product decision

TizenTube NX does not need a Google/YouTube account to provide an account-like experience.

For v1, profiles are **local-first TizenTube profiles** created directly on the Switch. A user can create a profile, choose a display name and avatar, and then build their own TizenTube library without Google authentication.

This is the primary account model for v1.

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

Actions such as Follow/Subscribe, Like, Watch Later and playlist edits modify the local TizenTube profile only. They do not write back to a Google/YouTube account.

Multiple local TizenTube profiles may be supported. A later UX may optionally associate a TizenTube profile with a Nintendo Switch user, but the TizenTube data model must remain independent of Nintendo and Google accounts.

## V1 security boundary

Google authentication is not part of the v1 profile architecture.

TizenTube NX must never request, accept, transmit, persist or log:

- Google passwords;
- Google OAuth access or refresh tokens;
- authenticated YouTube cookies;
- browser-session cookies;
- exported cookie files;
- equivalent Google account/session credentials.

The project should prefer an architecture in which these secrets never enter the TizenTube NX system at all.

This reduces both account-takeover risk and long-term maintenance caused by Google authentication changes.

## Post-v1 — YouTube to TizenTube migration

After v1, add an **optional import/migration feature** that can copy safely accessible YouTube data into an existing local TizenTube profile.

This is data migration, not Google sign-in.

The preferred UX is:

1. User chooses `Import from YouTube` on the Switch.
2. The Switch displays a QR code that can be scanned by a phone.
3. A short human-readable pairing code is also shown as a fallback.
4. The phone opens a temporary TizenTube import page/session.
5. The user supplies public YouTube channel/profile information and any public or unlisted playlist links they intentionally want to migrate.
6. The phone sends an import manifest to the paired Switch session.
7. The Switch shows an import preview and requires explicit confirmation.
8. Imported data is converted into local TizenTube profile data.
9. The temporary pairing/import session expires and is deleted.

The QR/pairing code must identify only an ephemeral import session. It must not contain Google credentials.

## Importable data

Where YouTube exposes the information publicly, or where the user explicitly supplies an unlisted playlist URL, the post-v1 importer may migrate things such as:

- public channel name/avatar metadata;
- public subscriptions/follows when exposed by the YouTube profile;
- public playlists;
- user-supplied unlisted playlists;
- playlist membership/order where available;
- public uploads/channel videos;
- canonical video/channel/playlist identifiers.

The importer should merge into the local profile without creating duplicates.

A later re-import may update an existing TizenTube profile while preserving local-only follows, playlists, favorites and history.

## Important limitation

Private YouTube account data cannot be discovered from a public channel without authentication.

For example, YouTube's built-in private Liked Videos data is not something TizenTube NX should attempt to obtain by bypassing the no-auth security boundary.

If a user wants to migrate otherwise-private selections without account authentication, a supported future workflow may allow them to create a normal public/unlisted migration playlist and provide that playlist to the importer.

## Pairing/import security goals

The future phone-to-Switch import path must be designed defensively:

- random, high-entropy one-time pairing sessions;
- QR primary UX plus short-code fallback;
- short expiration time;
- one-use completion semantics;
- rate-limited code attempts;
- explicit Switch-side confirmation before data is committed;
- no ability for a pairing session to control arbitrary Switch functions;
- no Google passwords, tokens or authenticated cookies;
- no analytics/advertising SDK requirement for the pairing page;
- no sensitive import payloads in logs;
- unlisted playlist URLs treated as secrets and redacted from diagnostics;
- temporary relay data deleted on completion or expiry.

Prefer end-to-end encryption for the import payload: the Switch generates the encryption secret, the phone encrypts locally, a relay stores only ciphertext, and the Switch decrypts locally. The relay should not need the plaintext import contents.

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

- QR + short-code phone pairing;
- public YouTube profile/list migration;
- public/unlisted playlist migration;
- duplicate-safe merge/re-import;
- encrypted temporary relay design;
- profile export/import/backup improvements as appropriate.

The post-v1 migration feature must not turn TizenTube NX into a Google-authenticated client.