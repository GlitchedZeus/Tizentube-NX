# M3 playback and extraction research — September 2026

This document preserves the implementation conclusions from the September 2026 Switch/YouTube research pass so future work does not restart the same investigation.

## Executive conclusion

The Switch-native player side is largely solved by existing homebrew technology. The extractor side is not one small native C++ parser anymore.

Recommended architecture:

- **Native Switch playback** using libmpv + FFmpeg + Deko3D + nvtegra.
- **Project-owned player abstraction** so app/UI code does not depend on raw mpv calls.
- **Provider-neutral YouTube extraction abstraction** from day one.
- Initial extraction provider should be a small remote/self-hostable service backed by current yt-dlp + EJS + PO-token support.
- Keep Google account credentials completely out of the Switch path for normal public playback.
- Preserve a future native extractor option without coupling player/UI to the provider implementation.

## Highest-value player references

### Switchfin

Repository: `dragonflylee/switchfin`

Highest-value files:

- `app/src/view/mpv_core.cpp`
- `app/include/view/mpv_core.hpp`
- `app/include/view/video_view.hpp`
- `app/src/activity/player_view.cpp`
- `app/src/view/player_setting.cpp`

Why it matters:

- Borealis-integrated libmpv;
- Deko3D render-context creation from `brls::SwitchVideoContext`;
- `ytdl=no` boundary;
- Switch direct rendering;
- hardware decode selection;
- pause/seek/property observation;
- buffering/progress/controller UI patterns;
- Apache-2.0 licensing is comparatively straightforward for adaptation.

Treat Switchfin as the preferred Borealis-facing player reference.

### SwitchWave / NXMP

Repositories:

- `averne/SwitchWave`
- `proconsule/nxmp`

Use primarily to validate nvtegra/FFmpeg/mpv backend behavior and codec expectations.

Evidence supports hardware decode paths for H.264/AVC, HEVC, VP8 and VP9 on Switch. Do not assume AV1 hardware decoding.

SwitchWave is GPL-3.0: study behavior/configuration unless license compatibility is explicitly handled.

### StreamFinEX

Repository: `Kirbosh/StreamFinEX`

Useful HTTPS/CDN resilience settings include larger stream buffering and FFmpeg reconnect/read-timeout options. Treat these as hardware-test candidates rather than unquestioned constants.

## Separate YouTube video + audio playback

DarkTube demonstrates the most useful YouTube-specific mpv pattern found:

1. `loadfile` the remote video-only URL.
2. Wait for `MPV_EVENT_FILE_LOADED`.
3. `audio-add <remote-audio-url> select`.

This means normal VOD playback does not inherently require downloading/remuxing video and audio locally or synthesizing a DASH manifest first.

DarkTube is a reference for this pattern, **not** the player configuration to copy wholesale. Its inspected player path disables hardware decode and also contains security choices that do not fit TizenTube NX.

Recommended combination:

- Switchfin/SwitchWave for hardware/rendering;
- DarkTube's separate remote A/V pattern;
- StreamFinEX resilience ideas.

## 2026 YouTube extraction reality

Do not architect M3 around:

`GET player JSON -> parse streamingData -> return URL`

Current robust extraction involves:

- Innertube client selection;
- signature and `n` challenge handling;
- current JavaScript challenge solving;
- increasingly, Proof-of-Origin (PO) tokens;
- client-specific fallback behavior;
- Googlevideo CDN/range behavior.

Current strongest practical stack:

- `yt-dlp`;
- `yt-dlp-ejs`;
- a maintained PO-token provider such as BgUtils.

Normal public playback does **not** inherently require a Google login. Guest/session state and PO tokens are distinct from authenticated Google cookies.

Do not build around old advice that `android_vr` permanently avoids PO enforcement. Current yt-dlp source records that the Android VR 1.65.10 configuration began returning 403 for all formats on 2026-08-17.

## Extraction-provider security

If a PO-token service is needed, do not expose a raw token provider directly to the Switch/LAN.

Preferred shape:

`Switch -> tightly scoped project extractor API -> localhost yt-dlp/EJS/PO provider`

The Switch API should accept a validated video identifier and return a bounded typed format result. It should not receive Google passwords, browser cookies, exported cookie files, or provider internals.

## Project-owned interfaces

Recommended player-side boundary:

```text
PlaybackSource
  videoUrl
  audioUrl
  mime/container
  videoCodec
  audioCodec
  width
  height
  fps
  videoBitrate
  audioBitrate
  expiresAt
  requiredHeaders

NativePlayer
  initialize()
  load(source)
  play()
  pause()
  stop()
  seekAbsolute()
  seekRelative()
  setQuality()
  getPosition()
  getDuration()
  isBuffering()
  getHwDecoder()
```

Recommended extractor boundary:

```text
YouTubeExtractor
  resolve(videoId)
  resolveLive(videoId)

ExtractionResult
  title
  duration
  isLive
  expiresAt
  videoFormats[]
  audioFormats[]
  captions[]
  requiredHeaders
```

Initial implementation may be `RemoteYtDlpExtractor`; a future `NativeExtractor` should be swappable without changing the player/UI contract.

## Initial codec/quality policy

Start conservative:

Video preference:

1. H.264/AVC MP4 <=1080p at the requested FPS;
2. lower-resolution H.264;
3. VP9 8-bit only after independent hardware validation;
4. muxed H.264 fallback.

Audio preference:

1. AAC/M4A initially;
2. Opus after independent testing.

Avoid as initial defaults:

- AV1;
- VP9 10-bit;
- HDR/high-bit-depth paths.

Goal: make SDR 1080p/1080p60 boring and reliable before expanding format complexity.

## Googlevideo transport must be a real subsystem

Current yt-dlp YouTube extraction uses a 10 MiB HTTP chunk target (`10 << 20`) for applicable formats. Recent reports also indicate large open-ended Googlevideo transfers can throttle while bounded range transfers retain speed.

Do not bury the entire media transport irreversibly inside one opaque `loadfile(URL)` call.

Start by testing stock mpv/FFmpeg behavior. Keep the architecture capable of adding an equivalent of:

`GoogleVideoChunkedReader`

or a tightly scoped proxy/custom stream callback if hardware proves it necessary.

Required M3 transport hardware gate should include:

- 20–60+ minute 1080p content;
- separate video/audio streams;
- repeated forward/back seeking;
- 60 fps where available;
- repeated byte ranges;
- reconnect behavior;
- expiring media URLs;
- verify no severe long-stream throttling.

## Future media network policy

Do **not** widen M2 now.

M2 remains exactly:

`www.youtube.com:443`

When M3 playback begins, introduce a separately reviewed media-plane policy rather than replacing default-deny with open HTTPS.

Likely media host family:

- validated `*.googlevideo.com:443` with exact suffix-boundary matching;
- `manifest.googlevideo.com:443` only if a selected playback path requires it.

Requirements remain:

- HTTPS only;
- reject URL credentials/userinfo;
- reject non-reviewed ports;
- revalidate redirects before DNS;
- Nintendo hard deny remains independent and first-class.

SponsorBlock is another separately reviewed destination and must not be smuggled into the media allowlist.

## SponsorBlock/ad-blocking research

TizenTube's current code is useful as a fixture/behavior reference for YouTube ad structures such as:

- `adPlacements`;
- `playerAds`;
- `adSlots`;
- `adSlotRenderer`;
- `reelWatchEndpoint.adClientParams.isAd`.

Do not port its global JavaScript-response patching strategy into the native app. Translate these structures into deterministic parser tests/firewalls.

SponsorBlock should remain independent from platform-ad suppression. Useful native design:

```text
SponsorBlockClient
  fetchSegments(videoId, categories)
  normalizeSegments(...)
  mergeOrResolveOverlaps(...)
  getNextAction(currentTime)
```

The player owns seeking. SponsorBlock failure must never block ordinary playback.

## NewPipe reference value

NewPipeExtractor remains highly valuable for parser/model/fixture ideas, especially:

- modern channel-tab structures;
- playlist structures;
- `lockupViewModel` handling;
- `gridVideoRenderer` / `gridPlaylistRenderer`;
- continuation actions;
- signature/`n` architecture;
- PO-provider abstraction.

Do not bet M3 completion on directly porting NewPipe extraction into C++ and assuming that alone solves current challenge/PO/SABR behavior.

For current M2 Channel/Playlist work, use NewPipe renderer lists as a **review checklist**, not permission for generic recursive traversal. Physical response shapes still decide which wrappers TizenTube NX explicitly admits.

## Recommended M3 order

### M3.1 — standalone hardware playback proof

Use one legal known HTTPS H.264/AAC test asset.

Prove:

- Deko3D/libmpv;
- `hwdec=nvtegra`;
- actual hardware decoder property;
- pause/play;
- repeated seeks;
- stop/re-enter;
- dock/undock.

### M3.2 — separate remote video/audio proof

Prove `loadfile(video)` + `audio-add(audio)` remains synchronized across pause/seek.

### M3.3 — extractor-provider API

Small bounded API keyed by video ID. Back it with current yt-dlp + EJS + PO-token support. Keep cookies/account credentials off the Switch.

### M3.4 — reviewed Googlevideo media policy

Add only the exact media-host rules proven necessary.

### M3.5 — range/chunk hardware validation

Long video, repeated seeks, verify sustained throughput.

### M3.6 — quality selection

Start H.264/AAC through 1080p60, then add VP9/Opus only after hardware proof.

### M3.7 — SponsorBlock

Use the privacy-preserving hash-prefix API design and controller-friendly per-category behavior.

### M3.8 — ad-bearing response fixtures

Lock in player/browse ad structures as host-test regressions.

## Do-not-waste-time conclusions

- Do not build a video decoder.
- Do not locally mux adaptive A/V merely to make normal VOD play.
- Do not build the extractor as C++ JSON parsing only.
- Do not rely on old Innertube-client bypass folklore.
- Do not open the whole internet because Googlevideo uses changing edge hosts.
- Do not ignore range/chunk behavior.
- Do not casually copy GPL code into differently licensed project files.

## Source index

Primary projects/references from the research pass:

- `dragonflylee/switchfin`
- `averne/SwitchWave`
- `Kirbosh/StreamFinEX`
- `proconsule/nxmp`
- `Ibnuard/DarkTube`
- `Ibnuard/darktube-server`
- `yt-dlp/yt-dlp`
- `yt-dlp/ejs`
- `Brainicism/bgutil-ytdlp-pot-provider`
- `TeamNewPipe/NewPipeExtractor`
- `reisxd/TizenTube`
- `ajayyy/SponsorBlock`

The original detailed research pass also identified exact source files/functions for these projects. Preserve the architectural conclusions above even if upstream implementation details move.
