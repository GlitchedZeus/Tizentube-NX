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

Project bootstrap is underway. The first foundation slice provides host-testable content filtering and canonical YouTube URL generation, plus a minimal libnx NRO entry point for the first hardware boot checkpoint.

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

The repository contains the first libnx entry point under `switch/source/main.cpp` and `Makefile.switch`. A devkitPro/devkitA64 environment is required for an NRO build. The Switch build will be expanded as Borealis, networking, authentication, and playback are integrated.

## License

GPL-3.0-only. See `LICENSE`.

TizenTube NX is an independent homebrew project and is not affiliated with Google, YouTube, TizenTube, ReVanced, or Nintendo.
