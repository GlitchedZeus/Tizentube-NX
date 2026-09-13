# TizenTube NX agent rules

## Product invariants

- Shorts never appear. Do not add a Shorts tab, player mode, shelf, recommendation, or enable setting.
- Never generate tracking-heavy YouTube share links. Construct canonical watch links from the validated video ID.
- No deliberate ads, promoted content, shopping shelves, Premium upsells, or unnecessary telemetry.
- V1 account UX is local-first TizenTube profiles. Users should be able to create a profile directly on the Switch with a name/avatar and build local follows, playlists, Watch Later, favorites/likes, history/resume and settings without Google authentication.
- V1 must not request or store Google OAuth material. Google account linking is post-v1 work only.
- TizenTube NX must never request, accept, transmit, persist or log Google passwords, passkeys, 2FA recovery codes, authenticated YouTube cookies, browser-session cookies, exported cookie files, or equivalent browser/account credentials.
- Post-v1, optional real YouTube linking may use Google's official limited-input/device-code OAuth flow. The phone must handle Google's real sign-in/consent UI; the Switch must never present a fake Google login form.
- Post-v1 OAuth must use least privilege, prefer read-only YouTube access, and initially support one-way `YouTube -> TizenTube profile` sync only.
- A short-lived OAuth access token should remain memory-only where practical. A refresh token may be persisted only if the user chooses to stay linked and only after a dedicated secure-storage review; it must never be logged, exported or stored as obvious plaintext on the SD card.
- Expired/revoked OAuth must degrade to `Reconnect YouTube` and must never destroy the local TizenTube profile or imported local data.
- Local TizenTube actions do not write back to Google/YouTube unless a future write-capable feature receives a separate security review and explicit user consent.
- A public/no-auth profile or playlist importer may remain as a post-v1 fallback for users who do not want OAuth linking.
- Preserve normal useful YouTube features unless they conflict with the privacy/de-bloat goals.
- See `docs/PROFILES_AND_IMPORT.md` and `docs/OAUTH_ACCOUNT_LINKING.md` before planning profile, account, migration, pairing, backup, token-storage or Google-related work.

## Feature-reference policy

- Treat `MorpheApp/morphe-patches` as a first-class feature and behavior reference for TizenTube NX, especially for ad/promotion filtering, SponsorBlock, playback controls, quality/audio preferences, privacy, sharing, player cleanup, DeArrow/thumbnail behavior, Return YouTube Dislike, captions, queues, livestream behavior, codecs/HDR, and other useful YouTube quality-of-life features.
- Prefer Morphe over ReVanced as a feature-inspiration source when they differ and Morphe better matches TizenTube NX goals. ReVanced remains a secondary reference.
- Do not blindly port Android bytecode patches. Translate useful Morphe behavior into independent native TizenTube NX architecture.
- Morphe Shorts behavior is reference-only: TizenTube NX must reject Shorts/reels entirely rather than merely hide them or open them in a normal player.
- See `docs/MORPHE_REFERENCE.md` before planning player/ad-blocking/polish work.

## Engineering rules

- Keep YouTube parsing/network code separate from UI.
- Put pure logic in `src/core` and cover it with host tests.
- Never log credentials, authorization headers, OAuth access/refresh tokens, cookies, device-code secrets after redemption, pairing secrets, or unredacted unlisted-playlist URLs.
- Every future auth/API/media/image/updater destination must pass the same outbound-policy review before DNS. Do not silently widen allowlists.
- Do not copy GPL code into a differently licensed file without preserving attribution/license obligations.
- Prefer small, testable commits and update `docs/PROJECT_STATUS.md` when a milestone changes.

## Validation order

1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build`
3. `ctest --test-dir build --output-on-failure`
4. When devkitPro is available: `make -f Makefile.switch clean all`
5. Hardware-test the resulting `.nro` before claiming a Switch milestone accepted.
