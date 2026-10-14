# M4 opt-in native Search → HLS → rolling mpv hardware lab

Updated 2026-10-13. This is a **development candidate**, not a released Switch player or evidence of compatibility on all YouTube videos.

## What is actually wired

On the experimental native build only (`TTNX_M3_INTEGRATED=1`, `TTNX_M4_ROLLING_STREAM=1`, `TTNX_M4_RING_STREAM_CB_PROBE=1`, `TTNX_M4_NATIVE_TEST_UI=1`), a vetted normal-video Search result displays **Play streaming (M4 hardware test)**.

1. The UI confirms a current normalized normal Search Video, valid identity, no live/upcoming item and no promoted/shopping/Shorts category. Arbitrary IDs from local history cannot activate the M4 route.
2. A background worker uses the existing in-memory guest session and `HlsSourceProbe`, with no account credentials, full response persistence or network work on the UI thread.
3. `admit_native_ring_plan` rejects wrong source IDs, ambiguous or separate audio, fMP4 init segments, empty/oversized playlists, invalid durations, unsupported >720p/>60fps/zero dimensions, unapproved CDN links, and manifest/source timestamp conflicts.
4. `create_native_ring_epoch` owns one native SSL segment reader, a finite MPEG-TS playlist and an exclusive fixed-name disposable SD ring.
5. The worker waits for **one completely validated H.264/AAC TS segment** before transferring the stream epoch to the UI thread. The prefix wait is capped at 35s **after plan resolution** (guest/manifest requests have their own timeouts). No whole-video download is needed.
6. The existing libmpv custom-stream adapter decodes the local `ttnxring://media` protocol; M4 source URLs never reach mpv. Decoder-close/worker-stop/SD unlink are prerequisites before any quota-bound restart.
7. Stop or cancellation rejects further CDN chunks and cleans up pending sessions. A blocked Horizon SSL operation may still require its native I/O timeout. Do not claim instantaneous Stop under all network conditions.

## Hardware gates still open

- Native `TTNX_M4_NATIVE_TEST_UI` devkitA64 compile/link **passed** on Actions run [38508943189](https://github.com/GlitchedZeus/Tizentube-NX/actions/runs/38508943189) (October 14, 2026). This is a build gate only, not evidence of Switch playback.
- Launching a *normal*, non-Shorts Search result on a real Switch and reproducing **both visible moving picture and selected AAC audio**.
- Sustained >10-minute playback, especially at the 24MiB-minus-reserve epoch boundary, without repeated buffering, lost frames, A/V drift, hangs or SD growth.
- Verify 720p and 60fps only on genuinely supported sources; no synthetic FPS promises.
- Pause/resume, verified seek semantics, sleep/wake, Stop from both buffering and playback, close/exit, crash-orphan cleanup.
- Max installed NRO + persistent data + active media <=100,000,000 decimal SD bytes; no downloads or thumbnail persistence, media removed at stop/exit.
- Network failure diagnostics without signed URLs, tokens, cookies or raw HTTP bodies.
- Real ad-free policy, SponsorBlock controls, quality/audio/caption settings and the final native layout require separate acceptance.

## Build isolation

The existing `.github/workflows/m3-integrated-player.yml` builds `tizentube_nx_m4_native_test.nro` under the four explicit flags, verifies the native objects, and **uploads an explicitly labeled, temporary M4 hardware-test artifact only when all native build steps pass**. It does not publish a release or replace the default NRO. The accepted M3 player remains available in the same test build and separately as the previously verified checkpoint. No MAIN merge or normal-menu enablement is implied. The artifact manifest must use the checked-out feature SHA, not GitHub's synthetic PR merge SHA.

Normal host and sanitizer coverage is in `CMakeLists.txt` and `.github/workflows/host-tests.yml`. The key tests are `native_ring_plan`, `rolling_stream_epoch`, `rolling_stream_cancel_propagation`, `rolling_ring_reader_concurrency`, `rolling_epoch_clock`, `rolling_reprime_gate`, `rolling_stream_epoch_long_vod`, plus `m4_native_search_route_contract_tests.py`.

**Do not mark these CI tests green until the jobs actually run.** A structural test is not a hardware test; 105 finite synthetic TS epochs do not prove seamless playback.
