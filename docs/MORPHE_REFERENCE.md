# Morphe reference policy

TizenTube NX treats [Morphe Patches](https://github.com/MorpheApp/morphe-patches) as a first-class feature and behavior reference for YouTube de-bloating, playback quality-of-life features, privacy, ad/promotion filtering, and player cleanup.

Morphe is preferred over ReVanced as a feature-inspiration source when the two differ and Morphe's behavior better matches the TizenTube NX product goals. ReVanced remains a useful secondary reference.

## How to use Morphe

Morphe patches the Android YouTube application, while TizenTube NX is a native Nintendo Switch client. Do not blindly port Android bytecode patches or assume Android implementation details apply to the NRO.

Use Morphe primarily as:

1. a feature catalogue for useful YouTube improvements;
2. a behavior/UX reference when designing equivalent native features;
3. a research source for YouTube player, ad, promotion, response, and UI changes;
4. an early-warning source for upstream YouTube behavior changes worth investigating in TizenTube NX.

Prefer independent native implementations that fit TizenTube NX's architecture and existing trust boundaries.

## High-value feature candidates

Future milestone planning should explicitly review Morphe for features such as:

- ad, promoted-content, Premium-upsell, and shopping suppression;
- SponsorBlock;
- DeArrow / alternative thumbnails and titles;
- Return YouTube Dislike;
- canonical / sanitized sharing links;
- default video quality and quality selection;
- custom playback speeds;
- original-audio preference and audio-track controls;
- captions/subtitle preferences;
- queue behavior;
- loop/reload controls;
- livestream resume/position behavior;
- fullscreen and player-layout cleanup;
- codec, HDR, AV1/VP9/H.264-related preferences where practical on Switch;
- useful player controls and de-bloating options that fit a controller-first UI.

This list is a backlog/reference source, not permission to implement unrelated features during an active milestone.

## Shorts policy overrides upstream behavior

Morphe's handling of Shorts is reference-only. TizenTube NX has a stronger invariant:

**Shorts/reels must not exist in the application.**

Do not add Shorts tabs, shelves, recommendations, playback, routes, placeholders, fallback handling, or an option to open Shorts in the normal player. A result identified as Shorts/reel content remains rejected by the existing firewall.

## Licensing boundary

`MorpheApp/morphe-patches` is GPL-3.0 licensed. Studying public behavior, architecture, patch names, and feature concepts is fine, but copying or adapting source code creates licensing and attribution obligations.

Default to independent native implementations. If source code is ever reused or adapted, review and preserve the applicable license, copyright, attribution, and notice requirements before merging it into TizenTube NX.

## Planning rule

Before major player, ad-blocking, SponsorBlock, thumbnail/title, quality/audio, privacy, sharing, or polish work, review the current Morphe patch catalogue and classify relevant ideas as:

- Definitely add
- Maybe / investigate
- Not applicable to native Switch
- Explicitly reject

Record worthwhile additions in the TizenTube NX roadmap rather than relying on chat history.