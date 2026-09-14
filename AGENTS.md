# TizenTube NX agent rules

## Product invariants

- Shorts never appear. Do not add a Shorts tab, player mode, shelf, recommendation, or enable setting.
- **Playback and an ad-free experience are co-equal requirements.** A player that deliberately shows YouTube-served pre-roll, mid-roll, post-roll, feed ads, promoted content, shopping cards, or Premium upsells is not considered finished playback.
- If a future upstream change prevents a safe ad-free playback path, fail clearly rather than knowingly falling back to ad playback and calling the player successful.
- SponsorBlock is part of the core player experience, not a distant polish feature. Provide controller-friendly per-category settings such as auto-skip, ask/show Skip button, and do-not-skip. Use SponsorBlock as the primary reference for community-defined in-video segment skipping.
- Keep platform-ad suppression and SponsorBlock conceptually separate: platform-ad suppression blocks YouTube-served advertising; SponsorBlock handles community-marked segments embedded inside the creator's video.
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
- See `docs/PRODUCT_PHILOSOPHY.md`, `docs/PROFILES_AND_IMPORT.md` and `docs/OAUTH_ACCOUNT_LINKING.md` before planning major player/profile/account work.

## Reference policy

Reference projects have distinct roles. Do not treat every upstream project as a general design authority.

- **YouTube** is the familiar information-architecture reference: Home, Search, Subscriptions, Library, channels, playlists, player controls, captions, quality and other concepts users already understand. Do not inherit its ads, Shorts, tracking-heavy sharing or engagement clutter.
- **TizenTube** is the primary UI/settings and day-to-day viewing-behavior guide. The target feel is a polished TizenTube/YouTube experience intentionally adapted to Nintendo Switch, not a generic homebrew browser.
- **SponsorBlock** is the primary behavioral reference for sponsor/self-promo/intro/outro and other community-defined segment skipping, including category semantics and user-configurable skip behavior.
- **Morphe** is a first-class feature-discovery and development-research source for future quality-of-life ideas such as player cleanup, quality/audio controls, DeArrow-style metadata, Return YouTube Dislike, privacy and other useful behavior. It does not define TizenTube NX's visual identity.
- **NewPipe** is a useful lightweight/privacy-conscious independent-client and technical/product reference where applicable.
- **ReVanced is not a current TizenTube NX philosophy/design reference.** Do not use it as the default source for product direction in new planning/docs.
- Do not blindly port Android bytecode patches or implementation details. Translate useful behavior into independent native TizenTube NX architecture.
- No reference project may weaken the no-Shorts invariant.
- Before M3 player/extractor work, read `docs/M3_PLAYBACK_EXTRACTION_RESEARCH_2026-09.md`. Its current architecture direction is Switch-native libmpv/FFmpeg/Deko3D/nvtegra playback behind a project-owned player interface, separate adaptive video/audio support, and a provider-neutral extraction boundary rather than a small hard-coded C++ YouTube parser.
- See `docs/PRODUCT_PHILOSOPHY.md` and `docs/MORPHE_REFERENCE.md` before major player/ad-blocking/SponsorBlock/polish work.

## Engineering rules

- Keep YouTube parsing/network code separate from UI.
- Put pure logic in `src/core` and cover it with host tests.
- Never log credentials, authorization headers, OAuth access/refresh tokens, cookies, device-code secrets after redemption, pairing secrets, or unredacted unlisted-playlist URLs.
- Every future auth/API/media/image/SponsorBlock/updater destination must pass the same outbound-policy review before DNS. Do not silently widen allowlists.
- Do not copy GPL code into a differently licensed file without preserving attribution/license obligations.
- Prefer small, testable commits and update `docs/PROJECT_STATUS.md` when a milestone changes.

## Validation order

1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build`
3. `ctest --test-dir build --output-on-failure`
4. When devkitPro is available: `make -f Makefile.switch clean all`
5. Hardware-test the resulting `.nro` before claiming a Switch milestone accepted.
