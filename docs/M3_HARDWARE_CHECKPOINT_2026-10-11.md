# M3 Switch hardware checkpoint — 2026-10-11

## Exact source / reproducibility

- GitHub repository: `GlitchedZeus/Tizentube-NX`.
- Draft PR: [#8](https://github.com/GlitchedZeus/Tizentube-NX/pull/8).
- **Exact physically tested NRO source commit:** `2acc3b7533d0e1ede0db0517b085ffd1179cb372`.
- Workflow: `M3 offline playback probe (isolated)`, run **38165382883** — success.
- NRO artifact ID: **11712017116**, `tizentube-nx-m3-OFFLINE-EXPERIMENT`.
- NRO SHA-256: `4a15677e26ad3c96ff9663fb7b4c69b8d976ce5dfff891156b1a4619d8063f36`.
- Host tests and standard Switch builds for this exact commit passed.
- The working offline NRO remains **separate** from the accepted M2 normal browsing NRO; do not replace it with an experimental player.

## What physical Switch hardware proved

The owner supplied direct handheld/homebrew screenshots showing the local 30-second
synthetic H.264/AAC file **actually playing**, with image output, progressing
time and render count, and audio described as audible but slightly quiet.

Observed app diagnostics:

- `Playback active. Decoder status shown below.`
- Video: `H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10`.
- Audio: `AAC (Advanced Audio Coding)`.
- Hardware-decoding report: **`nvtegra-copy`**.
- Displayed time advancing from approximately 3 to 5 seconds; frame rendering numbers advancing.
- Two-row UI fully visible, with navigable selection focus.

**Accepted:** H.264 MP4 local SD open, decode and picture output; AAC playback
(audible); libmpv's `nvtegra-copy` decoder reported in this specific
hardware configuration; fixed layout usability.

**Additional owner-confirmed physical control PASS (same exact Fix #4 NRO):**
pause and resume, seeking backward and forward, stopping playback, and
reloading/replaying the local MP4 after Stop. These operations worked on the
real Switch, beyond the earlier screenshots showing the movie playing.
The controls acceptance does **not** imply precise sample-level seek accuracy,
long-clip A/V synchronization, or robustness over multiple hours.

**Not yet accepted:** audio loudness of the boosted replacement fixture,
A/V sync under longer clips, dock/undock, sleep/wake, sustained framerate,
Deko3D direct rendering, arbitrary videos, remote CDN transports, YouTube
extraction/streaming, ads/SponsorBlock, and any installed-game or external
writes. The renderer currently uploads 640x360 software output to
Borealis/NanoVG even with a hardware-decoder path reported by mpv. Render-call
counter is not a measured FPS benchmark.

## Audio loudness observation and correction

The first test used FFmpeg's default `sine` generator, whose amplitude is
intentionally low. The fixture audio was measured on a host:

- Original synthetic AAC tone: approximately **-16.3 dBFS max peak**
  (mean approximately -21.1 dBFS).
- Re-encoded with **+10 dB** gain while byte-copying the H.264 video:
  approximately **-2.3 dBFS peak** (mean approximately -11.1 dBFS), with no
  digital clipping detected.

The replacement asset keeps the fixed path
`sdmc:/switch/TizenTube-NX/m3-test.mp4`; replacing **only** this MP4 does not
require changing the working NRO or any playback security setting.
Exact louder local test MP4 SHA-256:
`18f1b0e3f98c5fb73e2bb830c84b977e7050c5cf4580f4139dacda68a5f3cbf0`.

CI now generates an amplified synthetic tone for subsequent experimental
artifacts, with a peak-level gate. **The owner has not yet retested the boosted
tone on Switch**, so don't mark audio loudness resolved on hardware.

## Engineering boundaries

- Hard Shorts-free rule remains in force.
- The M2 accepted client retains `www.youtube.com:443` allowlisting and
  Nintendo hard-deny before DNS.
- Probe remains fixed-file offline only, with mpv scripts/config/ytdl disabled
  where present and required FFmpeg `protocol_whitelist=file`.
- Do not merge PR #8 or enable Googlevideo endpoints without separate policy
  and owner approval.

## Next hardware follow-ups

1. Replace the synthetic tone and confirm new relative loudness is comfortable.
2. Verify stable exit and repeated long-run control use. Basic pause, seek
   -10/+10 seconds, stop and reload have already passed physical testing.
3. Validate real audio/video synchronization over a longer legal offline clip,
   undock/dock and suspend/resume.
4. Decide if existing `nvtegra-copy` plus software presentation suffices for an
   interim 720p target or whether a reviewed Deko3D bridge is necessary.
5. Continue standalone stream extraction and policy-reviewed transport work
   independently; no networking is required to preserve the accepted offline
   checkpoint.
