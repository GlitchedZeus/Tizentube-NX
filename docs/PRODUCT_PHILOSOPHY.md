# TizenTube NX product philosophy

TizenTube NX should feel like a polished Nintendo Switch edition of the TizenTube/YouTube experience: familiar enough that a YouTube user immediately understands it, but deliberately stripped of ads, Shorts, tracking clutter and unnecessary engagement bloat.

The target is not "an experimental homebrew video browser." The target feeling is: **what if TizenTube had been intentionally designed for Nintendo Switch?**

## Playback and an ad-free experience are co-equal requirements

Video playback is not considered complete if the app still deliberately presents YouTube-served advertising.

**Playback that shows YouTube ads is not finished playback.**

The player milestone therefore includes both a working media pipeline and the ad-free contract:

- no pre-roll, mid-roll or post-roll YouTube ad playback;
- no ad units inserted into browsing/player surfaces;
- no promoted videos, shopping cards or Premium upsells presented as normal content;
- no fallback path that knowingly re-enables ads just to make playback succeed;
- no ad/tracking payload should bypass the normal parser/network policy merely because it is attached to a playable video.

If a future upstream change means TizenTube NX cannot safely obtain an ad-free playable result, failing clearly is preferable to deliberately showing an ad and calling playback successful.

This is separate from SponsorBlock. YouTube ad suppression handles platform-served advertising; SponsorBlock handles sponsor and other marked segments embedded inside the creator's actual video.

## SponsorBlock is part of the core player experience

SponsorBlock is not a distant polish feature. It belongs in the same broad player milestone as playback controls, seeking, captions and quality selection.

TizenTube NX should use SponsorBlock as the primary behavioral reference for community-defined in-video segment skipping.

The Switch UI should expose a clear SponsorBlock settings page. For supported categories, users should be able to choose behavior such as:

- **Auto-skip** — skip the category automatically;
- **Ask / show Skip button** — show a controller-friendly prompt and let the user choose;
- **Do not skip** — leave that category alone.

The exact category set should follow the SponsorBlock service at implementation time rather than freezing an outdated list in this document. Typical examples include sponsor segments, self-promotion, interaction reminders, intros/outros, previews and other community-marked segments.

Skipping should feel native on Switch: short unobtrusive feedback, controller-friendly actions, and no destructive UI transition. Settings must be easy to understand and reversible.

Any SponsorBlock API destination must still pass TizenTube NX's outbound-network review before DNS. Product priority never overrides the network-safety model.

## Reference hierarchy

Different upstream projects are references for different things. TizenTube NX should not treat every project as a general design authority.

### YouTube — familiar information architecture

Use YouTube as the baseline for concepts users already understand: Home, Search, Subscriptions, Library, channels, playlists, player controls, captions, quality, history and familiar media terminology.

Do **not** inherit YouTube's ads, Shorts, tracking-heavy sharing, engagement clutter or promotional surfaces merely for parity.

### TizenTube — primary UI and settings guide

TizenTube is the main behavioral/UI reference for how an ad-free YouTube experience should feel on a television/game-console style interface.

When designing navigation, player settings, ad-free behavior, SponsorBlock controls and other day-to-day viewing UX, prefer a TizenTube-like experience adapted naturally to Joy-Con / Pro Controller input and the Switch screen.

### SponsorBlock — sponsor/segment skipping reference

SponsorBlock is the canonical reference for community-defined in-video segment skipping, category semantics and the user controls around those skips.

It is not a replacement for platform-ad suppression; the two layers work together to deliver the project's ad-free viewing contract.

### Morphe — feature discovery and development research

Morphe is a feature catalogue and research source for ideas that may improve TizenTube NX later: playback quality-of-life controls, player cleanup, DeArrow-style metadata, Return YouTube Dislike, privacy improvements, codec/audio ideas and other useful behavior.

Morphe should inspire backlog items and help developers research YouTube behavior. It does **not** define TizenTube NX's visual identity, and Android patch implementation details must not be copied blindly into the native Switch architecture.

### NewPipe — lightweight client and technical reference

NewPipe is a useful reference for building an independent, privacy-conscious YouTube client with a small surface area and without making Google account credentials the center of the product.

Use it as a technical/product research reference where applicable, while keeping TizenTube NX controller-first and native to Switch.

## ReVanced is not a project philosophy reference

TizenTube NX does not use ReVanced as a product-philosophy or design reference.

Historical mentions may exist in old commits, but current planning and documentation should use the reference roles above: **YouTube, TizenTube, SponsorBlock, Morphe and NewPipe**.

## Shorts remain a hard rejection

Shorts/reels are not a configurable preference. They do not belong in TizenTube NX.

No reference project may override that rule. Do not add a Shorts tab, shelf, route, recommendation, player mode, fallback or enable setting.

## Definition of a successful viewing experience

A future playback milestone is only accepted when the whole experience works together on real hardware:

1. select a normal video;
2. start playback without a YouTube-served ad;
3. seek/pause/resume reliably;
4. use quality/audio/captions controls where supported;
5. apply the user's SponsorBlock category settings;
6. keep controller focus and overlays stable;
7. preserve the no-Shorts and clean-link rules;
8. keep all media/SponsorBlock/network destinations inside explicit reviewed outbound policy.

The goal is not merely to make pixels move. The goal is a clean, ad-free, Switch-native YouTube experience.