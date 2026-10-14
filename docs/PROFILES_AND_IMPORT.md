# TizenTube NX — local Library, history and storage contract

**Product decision, 2026-10-12: No Google/YouTube login, no account linking,
no OAuth, no cookies or credential import, in v1 or later planned releases.**

YouTube remains a source of public video and channel metadata, not the
owner of the user's TizenTube NX library. The app starts without sign-in
or local profile setup. A compact **on-device Library** provides familiar
personal features without storing video media.

## Local features

- **Watch history**: video ID, last-viewed time, small optional display
  title/channel metadata, completion state and last playback position.
  Update a single record when a video is revisited; no duplicate rows.
- **Continue watching**: resume position, with completed videos excluded
  or shown as completed according to user preference.
- **Watch Later**, **favorites/likes**, **followed channels** and
  **user-created playlists**: stable canonical video/channel IDs and
  compact ordering/labels, stored only on the Switch.
- **Search history**: optional, disabled or clearable independently.
- **History/library controls**: remove one item, clear all history, disable
  history collection, and manage local lists independently.
- The default library needs **no profile login**. Multiple *local-only*
  profiles or display avatars may be considered later, but must not create
  Google sign-in work, duplicate downloaded media or inflate v1.

Local "likes" and subscriptions/follows are **local bookmarks**, not
writes or likes/subscriptions submitted to YouTube. No misleading claim
that YouTube itself was updated.

## Small-footprint implementation requirements

- Store **metadata only**. Do not persist video/audio, full YouTube
  response bodies, preview frames, thumbnail image files, rendered art,
  signed playback URLs, cookies, OAuth material or tracking payloads.
- Cap the entire on-device Library to a proposed **2,000,000 decimal bytes**,
  inside the existing **8,388,608-byte total persistent-state allowance**.
  This is an engineering target requiring executable enforcement and may
  be reduced after measuring real usage, not a reason to preallocate files.
- Cap watch-history entries (initial proposal: **1,000 distinct videos**);
  evict the oldest history metadata deterministically when full. User
  playlists, favorites and Watch Later must have separately bounded quotas
  and must not be evicted silently as though they were throwaway history.
  Prefer explicit limit feedback for saved user collections.
- Compact IDs rather than URLs. Canonical watch sharing still constructs
  `https://www.youtube.com/watch?v=VIDEO_ID` on demand, without tracking.
- Persist with safe bounded writes and crash consistency. Include journal,
  backup and temporary file sizes in total app-owned SD accounting; do not
  create uncontrolled database WAL or duplicate backups.
- Avoid constant SD writes: batch progress updates, debounce history
  persistence, and flush cleanly on stop/exit without losing the last
  meaningful resume position.
- Let users clear records without affecting unrelated settings or other
  lists; a cleared history must not secretly accumulate elsewhere.
- While playing, total app-owned SD footprint — NRO, assets, persistent
  Library/settings/logs and the **approximately 25 MB temporary media
  stream cache** — must remain <= **100,000,000 decimal bytes**.
  Playback media is removed on stop and recovered on restart after crashes.
- Keep runtime RAM and startup overhead low; do not initialize a heavyweight
  browser, account services, analytics, database search index or background
  sync machinery for a local metadata Library.

## No Google-account integration

Do not add Google sign-in, OAuth device-code or QR linking, tokens, profile
synchronization, import of private Google/YouTube account data, cookie-file
login, password prompts, "Reconnect YouTube" or a post-v1 OAuth milestone.
Historical OAuth proposal documents are **withdrawn**.

Optional future export/import is strictly the user's own **local Library
metadata**, in a small versioned format. Public video/playlist links can
still be opened normally without account authentication.

## Engineering status

The existing M3 player is hardware-tested for video/audio playback.
The Library and bounded-history persistence are **planned features, not yet
implemented or validated**. Implement and test quotas, corruption handling,
crash consistency, repeated watch/resume and privacy before calling the
Library ready.
