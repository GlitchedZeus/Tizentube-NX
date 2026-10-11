# M3 standalone offline video probe — hardware acceptance checklist

This is **an experiment**, not the normal TizenTube NX NRO and not M3 completion.
The M2/PR #6 browse hardware baseline is unchanged. Do not replace the normal
\`sdmc:/switch/tizentube_nx.nro\` with this lab binary by accident.

## Why isolated

The pinned Borealis commit (\`20e2d33b6c4ffce139ce304c503c04f5b94da920\`)
does not expose the Deko3D device/framebuffer/fences that modern Switchfin
uses to feed mpv's direct Deko3D renderer. Replacing Borealis inside the
hardware-accepted M2 shell would reopen many focus/scroll/lifetime risks.

The isolated app tests \`libmpv\`'s **software framebuffer output** rendered
through Borealis/NanoVG at 640 x 360, including audio and basic controls.
It requests hwdec=auto and displays \`hwdec-current\`, but **this build does
not prove nvtegra hardware decoding, performance, 1080p or network playback.**
Those are later hardware gates using a reviewed graphics bridge.

## Build

\`\`\`bash
# devkitPro devkitA64 environment:
sudo dkp-pacman -S --needed switch-libmpv switch-libplacebo
git submodule update --init --recursive
make -f Makefile.switch TTNX_M3_OFFLINE_PROBE=1 -j2
\`\`\`

Or run the GitHub Actions workflow **M3 offline playback probe (isolated)**.
It emits **tizentube_nx_m3_probe.nro** and exact debug symbols, separate from
the normal production NRO. The workflow currently uses devkitPro packages
rather than a pinned mpv binary; record the package versions in hardware
reports so build reproduction can be improved after successful compilation.

The normal build remains exactly:
\`make -f Makefile.switch -j2\`
and does **not** import libmpv.

## Produce a redistributable test asset

Make a short synthetic H.264/AAC video on your PC; it contains no YouTube
media or copyrighted content:

\`\`\`bash
ffmpeg -f lavfi -i 'testsrc2=size=1280x720:rate=30' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000' \
  -t 30 -c:v libx264 -pix_fmt yuv420p -preset veryfast \
  -c:a aac -b:a 128k -movflags +faststart m3-test.mp4
\`\`\`

Put the MP4 on SD at:
\`sdmc:/switch/TizenTube-NX/m3-test.mp4\`.

Keep the original \`tizentube_nx.nro\` untouched. Put the experimental
\`tizentube_nx_m3_probe.nro\` alongside it using its distinct name.

## Device test

1. Boot the **M3 Offline Probe** NRO through Atmosphère homebrew.
2. Confirm the title is visibly distinct from normal TizenTube NX.
3. With the fixture absent, pressing Load should show a missing-file message,
   not crash, open a browser, or send any network request.
4. With the fixture in place, press **A** on the Load control and check that
   moving synthetic test video appears in the video rectangle.
5. Confirm the sine wave is audible and remains in sync.
6. Test **Y Pause/Resume**, **L/R 10-second seek**, **X Stop**, A reload, and
   B/+/homebrew exit. The on-screen buttons can be used instead.
7. Record the actual \`Decoder:\` readout. \`no\` does NOT pass the hardware
   decoder gate even if software output works.
8. Repeat after dock/undock and a sleep/wake cycle, recording any visual/audio
   failures. These are observations only, not presumed supported behaviors.
9. Record exact PR head, workflow artifact, NRO SHA-256 and pass/fail notes.

## Safety boundaries

- The binary can only load the exact fixed SD test path; it has no arbitrary
  URL/text/file picker, yt-dlp login, accounts, Googlevideo transport or
  SponsorBlock fetching.
- mpv config, scripts, ytdl and arbitrary input bindings are disabled.
- The FFmpeg demuxer network protocol whitelist is restricted to \`file,pipe\`.
- No approved M2 outbound-policy change is made and no Nintendo endpoint
  connectivity is introduced by application code.
- Linkage of third-party player dependencies is confined to the opt-in binary.
- If upstream mpv fails to support software output, **report the failure**.
  Do not enable network/video-web fallback or alter the accepted M2 NRO.
- A non-crashing, successfully compiled NRO is **not** proof of playback.

## What comes after this device gate

The next graphics step needs a reviewed switch video bridge exposing the
Deko3D device/framebuffer/fence ownership and the mpv render-context lifecycle
without breaking the pinned Borealis main application. Follow with native
player integration, a robust local-versus-remote media transport separation,
separate audio/video synchronization, verified nvtegra decoder, and finally
policy-reviewed YouTube sources/ad-free playback with SponsorBlock.

Reference: \`docs/M3_PLAYBACK_EXTRACTION_RESEARCH_2026-09.md\`.
