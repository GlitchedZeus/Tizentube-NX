# Project status

## Current phase

M1 — Switch application shell / pre-alpha.

## Accepted baseline

M0.1 is **device accepted** on a real Atmosphere Switch.

Verified on hardware:

- CI-built `tizentube_nx.nro` launches successfully from homebrew.
- Foundation screen renders as intended.
- The no-ads / no-Shorts product contract is visible in the device build.
- Canonical clean-link behavior remains covered by host tests.
- Reproducible devkitA64 Switch CI build is green.

## Implemented

- Pure C++ core library that can be tested without a Switch.
- Canonical video share URLs: `https://www.youtube.com/watch?v=<VIDEO_ID>`.
- Common YouTube watch/youtu.be/embed/live URL parsing with tracking data discarded.
- `/shorts/` URLs rejected by the canonicalizer.
- Hard content filter that always hides `ContentKind::Short` even if a future policy/config accidentally disables the setting.
- Default filters for promoted and shopping content.
- Device-accepted M0 console baseline; M1 replaces the entry point with Borealis.
- Host CI workflow.
- Reproducible devkitA64 NRO build workflow with uploaded artifact.
- Real Atmosphere hardware boot validation.

## Validation

Host CMake tests and the Switch devkitA64 build are green. The first generated NRO has been accepted on-device.

## Immediate next technical checkpoint

Hardware-test the M1 interface preview using `docs/M1_DEVICE_TEST.md`. Then begin M2 guest networking and renderer filtering. M1 is built, but is not device accepted.

## Known high-risk area

Account authentication remains the major future risk. The product requirement is easy console-style login without cookie-file import. Before building account-dependent UI, validate a sustainable TV/device authorization flow suitable for redistribution and avoid embedding third-party private credentials.

## M1 implementation checkpoint (2026-09-13)

- Replaced the text entry point with a native Borealis five-section shell.
- Added keyboard search entry (session only; no network results), system-theme
  styling, controller navigation and explicit tap hit-testing.
- Added app icon, embedded resources, shader build and pinned Borealis dependency.
- Added SD settings with temporary-file verification, backup recovery, and a
  bounded fixed-label boot record. Frame-rate display is the first saved setting.
- Local Release host build and both core/storage test suites pass.
- Native devkitA64 build PASSED at `3d869ebd88e57da4b2391401d78e16464ea71a06`; M1 is NOT device accepted.
- Hardware checklist: `docs/M1_DEVICE_TEST.md`.

The foundation no-ads text describes the product goal. Neither that screen nor
this interface preview proves ad-free playback; networking and playback remain
unimplemented. Authentication is still an unvalidated technical milestone.

## M1 build handoff

- Branch: `feature/m1-borealis-shell`; draft PR: https://github.com/GlitchedZeus/Tizentube-NX/pull/4
- Verified code commit: `3d869ebd88e57da4b2391401d78e16464ea71a06`
- Native CI: https://github.com/GlitchedZeus/Tizentube-NX/actions/runs/34735350633
- Artifact: https://github.com/GlitchedZeus/Tizentube-NX/actions/runs/34735350633/artifacts/10310687481
- NRO: `tizentube_nx.nro`, 1,864,638 bytes
- NRO SHA-256: `9c31ab733f03d33e6a202c574f4bf8c5e6d07ff0707309fb35a74e0856e10d70`
- Downloaded ZIP digest matches GitHub artifact metadata. NRO/ASET headers,
  embedded icon, NACP title, RomFS bounds, font and three shader entries checked.
- Host core/storage suites pass locally and in CI. Native build succeeds.
- Upstream compiler warnings remain (including Fontstash passing an already freed
  pointer to its no-op STB cleanup function); no project-source compile errors.
- Device boot, visual layout, touch/controller operation and dock transitions
  remain unverified for M1. This is an interface-only preview.
