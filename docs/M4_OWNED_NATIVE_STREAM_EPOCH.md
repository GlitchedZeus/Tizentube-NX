# M4 — single-owner native streaming playback epoch (2026-10-13)

This is a MAJOR opt-in integration tranche built from the existing
Switch-NewPipe-inspired libmpv callbacks, native Horizon SSL chunk
transport, MPEG-TS validator, and bounded SD ring.

## Actual source integration

`RollingStreamEpoch` now owns ONE verified-media producer thread, ONE
bounded SD ring, source segment URL list and ONE optional
`RollingMpvStreamBridge`. Its typed `with_reader` factory retains the
native HLS reader to the end of the worker lifetime. Segment sources
are checked **before any SD file is created** by a required URL
validator; the plan is capped at 4096 entries, each <=4096 bytes,
and <=512 KiB combined signed-URL metadata.

The producer passes every segment through the already verified
`VerifiedRingHlsIngest` path. Only complete, fsynced H.264/AAC
MPEG-TS data becomes visible to libmpv's `ttnxring://media`
callbacks. New data is delivered in <=32 KiB chunks and retained
validation metadata remains <=64 KiB. The ring's maximum physical
SD size is 24 MiB minus 128 KiB.

A decoder can register only after at least one completed media segment
exists, and only before `mpv_initialize`. The session exposes
read-only UI snapshots for prefix availability, committed segments,
physical file bytes, backpressure and producer join status. No
Borealis draw callback starts a CDN request.

The session makes a safe response to a full 25-MB-class ring:
**backpressure instead of overwriting media open by the decoder**.
The supported restart is a new seek epoch, and only AFTER
`request_stop -> worker.join -> mpv_render_context_free ->
mpv_terminate_destroy -> callback close -> ring unlink`. The caller
then recreates the same app-owned ring file at the requested segment.
This proves bounded multi-epoch operation, not yet seamless continuous
real-video playback.

## The native player is actually wired, but hidden

The opt-in `TTNX_M4_RING_STREAM_CB_PROBE=1` build now connects this
session to the existing hardware-tested `CachedMpv` class:

- `create_native_ring_epoch(plan)` validates a <=720p, <=60-FPS,
  nonempty, finite, **muxed** H.264/AAC HLS plan with approved
  CDN URLs. It rejects separate AAC, external audio, fMP4 init
  segments, unsupported plan metadata and duration disagreement.
  It owns the libnx HTTPS transport AND the HLS reader through the
  producer lifetime and uses fixed path
  `sdmc:/switch/TizenTube-NX/m4_stream_ring.part`.
- `open_m4_native_ring_candidate(epoch)` is deliberately **not
  called by Search or the main menu** pending native CI and physical
  verification.
- The normal `CachedMpv` creates the same software render context
  as M3, but, only in this experimental mode, registers the custom
  callback before `mpv_initialize` and loads the literal local
  `ttnxring://media` URI. mpv has no media-CDN network access.
- A ring replay or seek is **refused** until a fresh verified epoch
  is constructed; no absolute seek into deleted SD media is allowed.
- On teardown the player cancels the media producer, terminates
  libmpv/render context, releases its exclusive decoder lease,
  joins the worker and only then acknowledges decoder destruction
  and deletes the disposable ring.
- When the flag is disabled, the previous M3 media path, black-screen
  fix, menu actions and accepted player file URLs are unchanged.

## SD crash-recovery behavior

`RingOrphanCleanup` removes ONLY
`sdmc:/switch/TizenTube-NX/m4_stream_ring.part` on startup,
when no decoder is active. It checks the app-owned parent is a real
directory, and the file is a normal single-link file within the
hard SD cap. It refuses symlinks, oversized files, unexpected
paths, and any cleanup claimed while a decoder is active. This
startup check is intentionally active even after switching back to
the normal M3 NRO, so a prior experimental crash does not accumulate
watched media.

## Tests actually executed

Host normal (g++ C++20 -O2) and ASan/UBSan (-O1 -g) both PASSED
for each of the following, with assertions active even under -DNDEBUG:

- `rolling_stream_epoch_tests.cpp`: 5 synthetic H.264/AAC transport
  segments, 2-segment SD cap, reader FD denial, correct phase snapshots,
  3 decoder-closed epochs, validation-before-file-creation,
  no retained media on closing.
- `rolling_stream_epoch_real_av_tests.cpp`: real FFmpeg H.264/AAC
  MPEG-TS media; 2-segment cap, libmpv callback byte-for-byte readback,
  decoder-open cleanup denial, a new one-segment epoch, zero file
  after shutdown.
- `ring_orphan_cleanup_tests.cpp`: safe fixed-name orphan deletion,
  unknown user files preserved, no deletion while decoder active,
  oversized-file and symlink refusal.

The existing transactional ingest, stream callback and 100-MiB
synthetic/64-KiB ring suites passed in prior sessions but were
NOT all rerun against a complete repository checkout in this one.
New tests are registered under CTest and normal/sanitizer CI, with
the real-AV fixture generated from FFmpeg where available.

## Release blockers — do not misstate as accepted

- Full exact-head GitHub host and native devkitA64 workflows are
  still queued/pending; **do not claim a new NRO was compiled** until
  their actual result is inspected. The isolated M3 known-good artifact
  is NOT replaced or merged.
- The full native pipeline has NOT been run on physical hardware.
  720p/source-native FPS, audio synchronization, display, pause,
  responsive sleep/wake and RAM with sysmodules remain unmeasured.
- An active libmpv reader can pin the entire ~25MB ring.
  Hardware-proven mid-playback release, or a quality-tested
  decoder-close/reprime transition with acceptable visual continuity,
  is required before long-form playback is production-ready.
- The real YouTube ecosystem sometimes returns separate audio/video
  tracks. This probe still accepts **muxed H.264/AAC MPEG-TS only**.
- The M4 SSL worker may be blocked by a bounded native TLS timeout;
  never run its potentially blocking join on a Borealis draw callback.
- Storage acceptance includes NRO + assets + persistent Library +
  logs/settings + all temporary media <=100,000,000 **decimal** bytes.
  Low internal stream buffers do NOT prove the whole application has
  8 MB spare RAM on the actual modded Switch.

All third-party architectural attribution remains in
`docs/UPSTREAM_NATIVE_STREAM_BRIDGE.md` and
`docs/THIRD_PARTY_MEDIA.md`. No original YouTube NSP proprietary
binary, images, or Google credentials were imported.

## Post-review Shorts/provenance boundary

The native epoch factory now requires **both** an HLS probe plan AND
the corresponding already-filtered `core::ContentItem` from Browse.
It rejects any kind other than `ContentKind::Video`, a mismatched
normal-video ID, and anything rejected by the existing
`core::should_hide()` policy (including Shorts and promoted/shopping
items). This is an explicit data-provenance input, not an assumption
that a syntactically valid YouTube ID cannot belong to a Short.

The real video renderer and M3 launcher remain untouched; no
unauthorized menu entry into the experimental player was added.
