# TizenTube NX agent rules

## Product invariants

- Shorts never appear. Do not add a Shorts tab, player mode, shelf, recommendation, or enable setting.
- Never generate tracking-heavy YouTube share links. Construct canonical watch links from the validated video ID.
- No deliberate ads, promoted content, shopping shelves, Premium upsells, or unnecessary telemetry.
- Account UX should be console-like; do not make cookie-file import the primary sign-in path.
- Preserve normal useful YouTube features unless they conflict with the privacy/de-bloat goals.

## Feature-reference policy

- Treat `MorpheApp/morphe-patches` as a first-class feature and behavior reference for TizenTube NX, especially for ad/promotion filtering, SponsorBlock, playback controls, quality/audio preferences, privacy, sharing, player cleanup, DeArrow/thumbnail behavior, Return YouTube Dislike, captions, queues, livestream behavior, codecs/HDR, and other useful YouTube quality-of-life features.
- Prefer Morphe over ReVanced as a feature-inspiration source when they differ and Morphe better matches TizenTube NX goals. ReVanced remains a secondary reference.
- Do not blindly port Android bytecode patches. Translate useful Morphe behavior into independent native TizenTube NX architecture.
- Morphe Shorts behavior is reference-only: TizenTube NX must reject Shorts/reels entirely rather than merely hide them or open them in a normal player.
- See `docs/MORPHE_REFERENCE.md` before planning player/ad-blocking/polish work.

## Engineering rules

- Keep YouTube parsing/network code separate from UI.
- Put pure logic in `src/core` and cover it with host tests.
- Never log credentials, authorization headers, cookies, tokens, or redeemed device codes.
- Do not copy GPL code into a differently licensed file without preserving attribution/license obligations.
- Prefer small, testable commits and update `docs/PROJECT_STATUS.md` when a milestone changes.

## Validation order

1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build`
3. `ctest --test-dir build --output-on-failure`
4. When devkitPro is available: `make -f Makefile.switch clean all`
5. Hardware-test the resulting `.nro` before claiming a Switch milestone accepted.
