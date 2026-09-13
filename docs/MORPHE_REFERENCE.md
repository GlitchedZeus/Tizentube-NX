# Morphe reference policy

TizenTube NX treats [Morphe Patches](https://github.com/MorpheApp/morphe-patches) as a first-class **feature-discovery and development-research** reference.

Morphe is useful for finding YouTube quality-of-life ideas, player cleanup opportunities, privacy improvements, metadata enhancements, audio/quality controls, and other behaviors worth investigating in a native Switch client.

Morphe does **not** define TizenTube NX's visual identity, primary settings layout, or overall product philosophy. Those are guided primarily by YouTube familiarity and TizenTube's ad-free TV-style experience.

SponsorBlock is the primary reference for community-defined in-video sponsor/segment skipping behavior and settings.

ReVanced is not a current TizenTube NX philosophy/design reference.

## How to use Morphe

Morphe patches the Android YouTube application, while TizenTube NX is a native Nintendo Switch client. Do not blindly port Android bytecode patches or assume Android implementation details apply to the NRO.

Use Morphe primarily as:

1. a feature catalogue for useful YouTube improvements;
2. a behavior/research source when investigating equivalent native features;
3. a research source for YouTube player, promotion, response, and UI changes;
4. an early-warning source for upstream YouTube behavior changes worth investigating in TizenTube NX.

Prefer independent native implementations that fit TizenTube NX's architecture and existing trust boundaries.

## High-value feature candidates

Future milestone planning should explicitly review Morphe for ideas such as:

- DeArrow / alternative thumbnails and titles;
- Return YouTube Dislike;
- canonical / sanitized sharing behavior;
- default video quality and quality selection;
- custom playback speeds;
- original-audio preference and audio-track controls;
- captions/subtitle preferences;
- queue behavior;
- loop/reload controls;
- livestream resume/position behavior;
- fullscreen and player-layout cleanup;
- codec, HDR, AV1/VP9/H.264-related preferences where practical on Switch;
- privacy/de-bloat ideas;
- useful controller-friendly player controls.

Ad-free playback itself is not merely a Morphe-inspired enhancement. It is a TizenTube NX product invariant defined in `docs/PRODUCT_PHILOSOPHY.md` and `docs/SCOPE.md`.

SponsorBlock category behavior/settings should be researched against SponsorBlock itself rather than treating Morphe's integration as the canonical definition.

This list is a backlog/reference source, not permission to implement unrelated features during an active milestone.

## Shorts policy overrides upstream behavior

Morphe's handling of Shorts is reference-only. TizenTube NX has a stronger invariant:

**Shorts/reels must not exist in the application.**

Do not add Shorts tabs, shelves, recommendations, playback, routes, placeholders, fallback handling, or an option to open Shorts in the normal player. A result identified as Shorts/reel content remains rejected by the existing firewall.

## Licensing boundary

`MorpheApp/morphe-patches` is GPL-3.0 licensed. Studying public behavior, architecture, patch names, and feature concepts is fine, but copying or adapting source code creates licensing and attribution obligations.

Default to independent native implementations. If source code is ever reused or adapted, review and preserve the applicable license, copyright, attribution, and notice requirements before merging it into TizenTube NX.

## Planning rule

Before major player, metadata, quality/audio, privacy, sharing, or polish work, review the current Morphe patch catalogue and classify relevant ideas as:

- Definitely add
- Maybe / investigate
- Not applicable to native Switch
- Explicitly reject

For ad-free playback and SponsorBlock work, also review `docs/PRODUCT_PHILOSOPHY.md`; SponsorBlock itself remains the primary segment-skipping reference.

Record worthwhile additions in the TizenTube NX roadmap rather than relying on chat history.
