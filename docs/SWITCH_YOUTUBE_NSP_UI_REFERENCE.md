# YouTube v2.0.0 patched NSP: verified container analysis and UX reference

**Reviewed:** 2026-10-12 | **Target:** TizenTube NX
**Source:** user-supplied `YouTube v2.0.0 (NSP)(Patched).rar` (private inspection only)
**Purpose:** identify transferable interaction concepts and distinguish them
from encrypted/proprietary application internals. **No official binary or
assets were committed to this repository.**

## Actual archive inspection (verified from supplied bytes)

The 70-MB-class RAR5 archive contains **exactly one** 72,389,840-byte NSP.
The NSP unpacked through libarchive with a verified RAR CRC32 of
`f1dae83c` and SHA-256
`e45a0fa3e9eae411d1c8bfe73912018a1d5a7a4881d48048f8a94c53cb731014`.

The NSP is a well-formed **PFS0**: 3 directory entries, 120-byte filename
table, and 208-byte container header.

| Entry | Exact bytes | Observed container role |
| --- | ---: | --- |
| `71a540b78574913c68b57b26d6db034a.nca` | 72,235,008 | Main (largest) NCA; exact content type unverified |
| `ec5fafb5ee9c512710b597eab6afc76a.nca` | 150,528 | Secondary NCA; exact content type unverified |
| `2c26530ef32ac54b29dbb88e49a649fd.cnmt.nca` | 4,096 | Content metadata NCA by filename |
| **NSP** | **72,389,840** | Includes 208-byte PFS0 header |

The ordinary readable `NCA3` header magic is **not visible** at offset
0x200 of any entry. The 256-KiB samples from the two larger NCAs have
~7.999 bits/byte Shannon entropy, consistent with encrypted content.
Neither sample nor package metadata gives direct access to RomFS assets,
JavaScript, application UI trees, or video decoder implementation.
No decryption keys were supplied, so **no decrypted NCA, ExeFS, RomFS,
screens, scripts, media-engine code, codecs, or API behavior were extracted**.

"v2.0.0", "Patched" and title ID `05003A400C3DA000` originate from the
archive filename **only**; internal version, patch specifics, signature
status and developer intent could not be authenticated with this
encrypted NSP. Do not infer that it contains a specific software player,
hardware video decoder, 720p/60 support, codec, caching system, or
Nintendo/Google account bypass.

## Publicly documented official Switch YouTube UX (independent evidence)

These observations are from screenshots and Nintendo/Google public
documentation, **not decrypted from the uploaded package**.

- Nintendo and contemporary screenshots show a dark, television-style
  browsing canvas with a left-side navigation area, wide 16:9 video
  tiles, short titles/channel metadata and horizontal/row browsing.
- The fullscreen player reference uses unobtrusive overlays with a
  timeline, play/pause, captions, and next-video suggestions.
- Nintendo documents control via D-pad or left stick, selecting a video,
  navigating a progress bar with Joy-Con or touch, and finding captions
  in the player. Google documents **X** for search, **A** play/pause,
  **B** to leave player, and a quality selector in More.
- Nintendo describes the Switch version as a console/TV-style app.
  **No public support source in this review identifies its actual
  internal decoder implementation, streaming architecture or RAM usage.**

Public source URLs reviewed:
- https://en-americas-support.nintendo.com/app/answers/detail/a_id/42563/~/using-youtube-on-nintendo-switch
- https://support.google.com/youtube/answer/9176176?hl=en
- https://en-americas-support.nintendo.com/app/answers/detail/a_id/42550/p/897
- https://www.nintendo.com/en-ca/store/products/youtube-switch/

## Original TizenTube NX UI adaptation (implementation decisions)

**Do not copy** Google's copyrighted bitmaps, thumbnails, branding,
commercial fonts, source code, emulator patches or decrypted binaries.
Build our own artwork/layout/controller logic with Borealis + NanoVG.

| Area | Reference concept | TizenTube NX implementation |
| --- | --- | --- |
| Main navigation | Compact left TV navigation | Home, Search, Library, Following, Settings; no Google profile/sign-in, Shorts, promotions or Premium tabs |
| Discovery | Large image-first 16:9 rails | 3-ish visible cards across TV width; 16:9 frames, durations and stable focus ring; load thumbnail resources lazily with bounded RAM |
| Local history | Watch-again/library surfaces | On-device History and Continue Watching from canonical video IDs and resume timestamps, with a proposed <=2,000,000-byte Library metadata budget |
| Search | Direct controller shortcut | X opens search; native Switch keyboard; predictable B return and joystick/D-pad focus parity |
| Player | Content-first fullscreen | Reuse hardware-accepted M3 video render path; no debug/status labels on accepted end-user video canvas |
| Controls | Short-lived lower overlay | Progress/scrub, title, play/pause, seek, captions if available, and quality <=720p at actual source frame rate |
| Focus | Television-remote patterns | Show focused control without losing video, restore focus on dismiss; back closes overlay first where appropriate |
| Suggestions | Post-play rails | Normal videos only; no autoplay Shorts, promoted units, account-only UI or tracking links |
| Settings | Small and local | Quality, captions, SponsorBlock, video history, cache diagnostics and local library controls |
| Performance | Minimal UI overhead | No browser/WebView/Google application bundles; one accepted libmpv renderer, lazy art, short-lived overlay and small navigation state |

**Current code comparison (2026-10-12 M4 branch):**
- `switch/source/main.cpp` already has a Borealis `TabFrame` and
  focus-aware Search/Channel flows, but Search is a mostly vertical
  results view rather than TV-style large thumbnail rails.
- `switch/source/m3_player_activity.cpp` owns the accepted player and
  shows a **294px-high** `VideoFrame` plus several always-visible
  diagnostics labels and two rows of explicit buttons. The final
  video-first fullscreen overlay remains a separate UI task.
- The M3 player uses the local libmpv software-render callback and
  bounded rendered texture. The user physically verified real picture
  and audio on a ten-minute YouTube video. **Preserve that baseline**
  instead of trying to lift a protected playback engine out of the NSP.
- Current accepted controls use Y for pause, X stop, L/R +/-10 seconds
  and B return. Reworking toward console-YouTube-style A/pause and a
  bottom timeline requires explicit regression tests and user controls
  that never accidentally stop playback. Existing hardware behavior
  should not be silently changed by a research-only PR.
- The user confirmed **no Google accounts**, a tiny local metadata
  Library, zero saved watched videos, and a ~25 MB **rolling SD** cache
  (not a 25-MB RAM allocation) inside a **100-MB total SD ceiling**.

## Video playback lessons: usable vs not substantiated

**Can reuse as an interaction model:** fast start, controller-first
pause/resume and seeking, quality selection, captions when supported,
clean overlay show/hide, safe B/back flow, and local watch history.

**Cannot claim from this encrypted package:** actual network protocols,
libmpv/FFmpeg/Chromium use, hardware decoder, codec implementation,
signed URL handling, 720p/60 throughput, bandwidth adaptation, buffer
sizes, proprietary account patch implementation, or a working way to
stream media without writing whole segments into RAM.

**Engineering plan**:
1. Keep M3's hardware-good renderer intact. Build the original
   full-screen overlay as a clearly isolated, reversible UI candidate.
2. Enforce the explicit SD-backed **rolling** stream design, including
   a total temp-media quota ~25 MB and small bounded network-to-file
   buffers. It must *not* download an entire video before playback.
3. Resolve player ownership of played HLS segment FDs before deleting
   segments; otherwise long-form streaming stalls at quota.
4. Preserve selected AAC, quality <=720p, source-native fps, clean
   URL policy, SponsorBlock and Shorts/ad blocking; independently test.
5. Add the 2-MB-or-less local History/Continue Watching metadata-only
   library; no Google account or OAuth ever.
6. Measure total NRO+assets+settings/history/logs+SD-media <=100 MB
   and peak RAM with real sysmodules present on physical Switch.
7. Normal/sanitizer host tests, native devkitA64 compile and on-device
   10+ minute playback must pass before changing accepted launcher.

## Direct screenshot evidence now supplied

The user subsequently supplied **37 actual 1280×720 screenshots** from
the patched Switch app. This resolves the earlier **visual layout**
evidence gap: player timeline/control overlay, right-hand Quality and
Audio menus, channel banner/shelves, Following, local Library, Search,
Settings, and both sidebar states are directly visible. See
[SWITCH_CAPTURE_LAYOUT_REVIEW.md](SWITCH_CAPTURE_LAYOUT_REVIEW.md) for
approximate positions, intended TizenTube NX design, privacy precautions
and tested standalone native-layout geometry. The official NSP's
**decoder and source internals remain encrypted/unverified**. Do not
publish the user's captured account, history or pairing information,
and do not request credentials or console keys for UI design.

This report intentionally contains **no Nintendo/Google NCA bytes,
keys, decrypted copyrighted resources, or extracted code**.
