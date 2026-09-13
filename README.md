# TizenTube NX

A native, controller-first YouTube client for Nintendo Switch homebrew (Atmosphere), inspired by TizenTube and ReVanced.

## Product rules

- **Shorts do not exist.** They are filtered at the data boundary and never rendered.
- **No YouTube ads or promoted content.**
- **Clean sharing only.** A video with ID `IzJ7R4EnYmI` shares as exactly `https://www.youtube.com/watch?v=IzJ7R4EnYmI`.
- **Normal YouTube account features stay useful.** The target is simple TV/game-console style sign-in, subscriptions, personalized feeds, history/progress, playlists, likes, channels, captions, and normal playback controls.
- **Remove bloat/tracking.** No tracking parameters in outgoing links; avoid unnecessary telemetry and engagement clutter.
- **SponsorBlock + DeArrow** are first-class enhancement goals.
- **Switch-first UX.** Joy-Con / Pro Controller navigation, handheld and docked layouts, with touch where it helps.

## Status

M0 is device accepted. The M1 branch adds a Borealis interface preview with five sections, native keyboard entry and saved display preferences. YouTube networking, sign-in and playback are not implemented yet. See `docs/M1_DEVICE_TEST.md` for the next device check.

See:

- `docs/SCOPE.md`
- `docs/ARCHITECTURE.md`
- `docs/ROADMAP.md`
- `docs/PROJECT_STATUS.md`

## Foundation behavior

```text
Video ID: IzJ7R4EnYmI
Share URL: https://www.youtube.com/watch?v=IzJ7R4EnYmI
Shorts item: blocked
```

## Host build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tizentube_nx_host
```

## Switch build

Requires devkitPro/devkitA64, libnx, deko3d and the uam shader compiler.

```bash
git submodule update --init --recursive
make -f Makefile.switch -j2
```

The CI workflow builds with the devkitPro container and uploads the NRO. Resources
and shaders are embedded, so only the NRO needs copying to the SD card. Preferences
and a bounded boot log use `sdmc:/switch/TizenTube-NX/`. See `docs/THIRD_PARTY.md`
for dependency attribution.

## License

GPL-3.0-or-later. See `LICENSE`.

TizenTube NX is an independent homebrew project and is not affiliated with Google, YouTube, TizenTube, ReVanced, or Nintendo.
