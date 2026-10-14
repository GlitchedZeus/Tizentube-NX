# M4 native TV player — isolated implementation and safety gates

This is **real source implementation** of an original Switch TV-player
presentation based on the user's 37 screenshots, not extracted NSP code.

## Implemented and gated

With `TTNX_M3_INTEGRATED=1 TTNX_M4_TV_PLAYER_UI=1` only:
- The M3 player activity displays the existing rendered H.264/AAC picture
  in a much larger **602 px video surface** rather than the 294 px
  developer/test window. Its NanoVG image lifecycle, visible-frame
  alpha fix, libmpv renderer and decoder lease remain unchanged.
- A dark, automatically hiding lower transport includes a playback
  progress bar from `time-pos/duration` and a play/pause graphic.
  The existing UI tick drives it with no timers, extra decode threads,
  second video texture, or new SD files.
- A minimal controller row provides Replay, Pause/Resume, -10s, +10s,
  Details and Exit. B closes the active transport first, then exits
  on a subsequent press; X toggles controls, Y pauses, L/R seek.
- Detailed decoder status/stats are hidden by default and shown only
  via Details, avoiding always-visible developer strings.
- With the flag **OFF**, the accepted M3 screen, playback controls,
  alpha fix and the renderer remain in their prior branches untouched.

The original screenshot's right-side Quality/Audio panels are **not yet
functional** because source-quality reselection and optional audio DSP
must not be exposed as fake actions. The new view is an actual
video-first implementation, **not** a claim of visual parity with every
YouTube screenshot.

## Host regression and native checks

`include/tizentube_nx/ui/tv_player_controls.hpp` is a tiny standalone
model for transport/diagnostics visibility, auto-hide, pause behavior,
back navigation and clamped progress/seek numbers. Its tests are
`tests/tv_player_controls_tests.cpp`, registered in CTest and CI.

Executed locally:
```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  -Iinclude tests/tv_player_controls_tests.cpp -o /tmp/tv-controls
/tmp/tv-controls
g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude tests/tv_player_controls_tests.cpp -o /tmp/tv-controls-san
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/tv-controls-san
```
Both tests passed against the exact Git blobs committed.

The dedicated M3 integrated GitHub workflow now **also builds** a
separate `tizentube_nx_m4_tv_preview.nro` with the opt-in flag,
verifies the native player object and 66,445,568-byte NRO cap, and
does **not** upload that unproven candidate as a user artifact.
The accepted M3 build remains the only uploaded hardware candidate.

Pending acceptance: real native devkitA64 build, focused controls,
visible frame/picture and AAC audio, correct parent alpha
on opening/closing, repeated playback, seek, stop, sleep/wake,
and no GPU images leaked after leaving/reopening the activity.

The bounded ~25 MB rolling SD stream and 100 MB total installed
footprint remain distinct hard requirements. This UI work deliberately
does **not** switch on the experimental SD stream, pretend network
decoding is ready, or change the working M3 media path.
