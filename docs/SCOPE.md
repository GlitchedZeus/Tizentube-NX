# Product contract

## Non-negotiable behavior

### Shorts do not exist

TizenTube NX does not implement a Shorts experience. Feed parsers, search results, channel shelves, recommendations, subscriptions, notifications, and deep-link handling must discard Shorts before presentation.

There is intentionally no user-facing setting to enable Shorts.

Do not infer Shorts solely from duration; use explicit YouTube renderer/endpoint/type metadata so legitimate short-form normal videos are not accidentally removed.

### Ads and promoted content do not belong in the UI

The application should not deliberately present YouTube ad units, promoted video shelves, shopping cards, Premium upsells, or similar bloat.

### Clean sharing

For a video ID `VIDEO_ID`, the share action constructs this URL locally:

```text
https://www.youtube.com/watch?v=VIDEO_ID
```

Do not reuse a server-provided tracking URL. Do not append `si`, `feature`, `utm_*`, or equivalent tracking parameters.

### Account UX

The desired sign-in experience is console-like: initiate sign-in on Switch, approve on a phone/browser, then persist renewable credentials securely enough for a homebrew SD-card environment. Cookie-file import is not an acceptable primary login UX.

### Useful YouTube, not feature parity

Keep normal account/browsing/playback features that make YouTube useful. New YouTube engagement clutter is not automatically in scope.
