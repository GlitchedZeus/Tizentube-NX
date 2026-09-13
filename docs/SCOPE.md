# Product contract

## Non-negotiable behavior

### Shorts do not exist

TizenTube NX does not implement a Shorts experience. Feed parsers, search results, channel shelves, recommendations, subscriptions, notifications, and deep-link handling must discard Shorts before presentation.

There is intentionally no user-facing setting to enable Shorts.

Do not infer Shorts solely from duration; use explicit YouTube renderer/endpoint/type metadata so legitimate short-form normal videos are not accidentally removed.

### Playback must be ad-free

Video playback and ad suppression are co-equal product requirements.

A playback milestone is not considered complete if TizenTube NX deliberately presents YouTube-served pre-roll, mid-roll, post-roll, feed ads, promoted videos, shopping cards, Premium upsells, or equivalent ad/promotional surfaces.

Do not knowingly fall back to ad-bearing playback just to make a video start. If an upstream change prevents a safe ad-free playback path, fail clearly and investigate rather than silently weakening the product contract.

### SponsorBlock is part of the player, not a later extra

SponsorBlock is the primary reference for community-defined in-video segment skipping.

The Switch settings UI should support clear per-category behavior such as:

- Auto-skip
- Ask / show Skip button
- Do not skip

Keep this separate from YouTube ad suppression: SponsorBlock handles segments embedded in the creator's video; platform-ad suppression handles YouTube-served advertising.

### Ads and promoted content do not belong in the UI

The application should not deliberately present YouTube ad units, promoted video shelves, shopping cards, Premium upsells, or similar bloat.

### Clean sharing

For a video ID `VIDEO_ID`, the share action constructs this URL locally:

```text
https://www.youtube.com/watch?v=VIDEO_ID
```

Do not reuse a server-provided tracking URL. Do not append `si`, `feature`, `utm_*`, or equivalent tracking parameters.

### Account UX

V1 is local-first and must not require a Google/YouTube account. Optional official device-code OAuth linking is post-v1 work only.

Cookie-file import, password collection, browser-session cookie import, or equivalent credential scraping is not an acceptable primary account UX.

### Useful YouTube, not feature parity

Keep normal browsing/playback features that make YouTube useful. New YouTube engagement clutter is not automatically in scope.

## Product/reference hierarchy

- **YouTube** provides familiar concepts and information architecture.
- **TizenTube** is the primary UI/settings and viewing-behavior guide.
- **SponsorBlock** is the primary segment-skipping behavior/settings reference.
- **Morphe** is a feature-discovery and development-research source for future improvements.
- **NewPipe** is a useful lightweight/privacy-conscious independent-client reference.
- **ReVanced is not a current product-philosophy/design reference.**

See `docs/PRODUCT_PHILOSOPHY.md` for the full contract.
