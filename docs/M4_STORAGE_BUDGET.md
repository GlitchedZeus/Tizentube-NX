# M4 storage and latency target: ≤100,000,000 bytes total

Product target: the TizenTube NX NRO (including embedded assets), all app-owned
settings/diagnostics, and the **maximum simultaneous disposable video/audio
buffer** must consume no more than **100,000,000 bytes** of SD storage.

Proposed auditable budget:
- NRO + embedded assets: at most 66,445,568 bytes (CI size gate).
- Rolling video/audio working set: at most 25,165,824 bytes (24 MiB).
- App-owned settings, essential logs and metadata: at most 8,388,608 bytes
  (8 MiB; future runtime enforcement required).

The sum of these three reservations is exactly 100,000,000 bytes, not 100 MiB.
No saved videos, thumbnails library, watch history, analytics telemetry,
advertising IDs, or unbounded diagnostics may live outside this budget.

**Not yet achieved:** M3 currently downloads full temporary video/audio
before regular playback; its existing native HLS and MP4 cache path can still
use substantially more than 100 MB. The pure RollingMediaBudget scheduler
added here proves accounting and safe eviction rules under normal and
sanitizer host tests, but is not wired to mpv, the native cache or the
network transport. No new NRO should be labeled 100-MB compliant until the
streaming pipeline, persistent-state quota and cleanup/seek gates all pass
on actual hardware. The accepted M3 renderer is deliberately untouched.

Required next steps:
1. Build a verified 720p-or-lower streaming source with early H.264/AAC
   readiness. Target a *few seconds* of decoded-ready media; do not promise
   zero startup delay for slow networks or unavailable media.
2. Stage a local-only playlist/transport that libmpv can consume while
   producer fetches further verified media. Keep signed CDN URL handling
   inside the existing approved native transport. Do not grant libmpv
   direct Internet protocols.
3. Pin active decoder segments, unlink behind a two-second protection
   window only after the file is closed, then commit reclaimed quota.
   Back-pressure producers when the 24 MiB reservation is full.
4. On seeks outside the retained window, cancel and join producers, release
   decoder leases, purge old segments, then reopen a verified seek window.
   Fail safely on CDN expiry, sleep/wake, network drops or SD write failures.
5. Validate stable A/V sync, 720p scaling, native available frame rate,
   speed, no temporary-media growth over long videos, and cache=0 at exit.
6. Enforce an actual 8 MiB limit on app-owned settings and diagnostic logs
   and verify the end-to-end 100 MB cap on hardware.

Privacy and interface intent: no Shorts, ads or promoted content, no
app-added tracking or analytics, no persistent watched-video cache, and
canonical share URLs. Network requests to YouTube/CDN are still necessary
to stream video; do not misrepresent this as invisible or anonymous to
the upstream provider. Preserve user-initiated settings only as needed.

**Release gate:** all host normal/ASan/UBSan checks, native Switch build,
NRO size check and on-device full-view 720p video/pause/seek/cleanup test.
No automatic merge of PR #8 and no regression to accepted real playback.
