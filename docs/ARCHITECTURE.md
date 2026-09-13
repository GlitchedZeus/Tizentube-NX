# Architecture

## Layers

1. **platform/** — libnx, filesystem, networking lifecycle, controller/touch integration.
2. **ui/** — Borealis activities/views, TV-style navigation, settings, account surfaces.
3. **youtube/** — request models, TV/Innertube transport, renderer parsing, pagination.
4. **auth/** — device-code/TV login state machine, token storage/refresh, sign-out.
5. **playback/** — format selection, mpv/FFmpeg handoff, quality, captions, audio tracks.
6. **enhancements/** — SponsorBlock and DeArrow adapters.
7. **core/** — pure logic usable by host tests: content policy, URL canonicalization, models.

Dependencies should point inward toward `core`; YouTube parsing must not directly own UI widgets.

## Initial risk order

1. Verify sustainable account authentication on a distributed homebrew client without asking users to export cookies or embedding someone else's private OAuth credentials.
2. Build a minimal authenticated account probe (profile/subscriptions/home data).
3. Establish robust playback/format resolution on real Switch hardware.
4. Add the Borealis browsing UI.
5. Integrate SponsorBlock and DeArrow.
6. Harden caching, storage, error recovery, and applet/title-takeover behavior.

## Privacy rules

- Generate share URLs locally from IDs.
- Persist only data required for settings/session/account features.
- Keep tokens out of logs.
- Never log cookies, authorization headers, refresh tokens, or device codes after redemption.
- Do not add analytics/telemetry by default.
