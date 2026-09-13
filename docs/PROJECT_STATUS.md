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
- Minimal libnx console app source and devkitPro makefile.
- Host CI workflow.
- Reproducible devkitA64 NRO build workflow with uploaded artifact.
- Real Atmosphere hardware boot validation.

## Validation

Host CMake tests and the Switch devkitA64 build are green. The first generated NRO has been accepted on-device.

## Immediate next technical checkpoint

M1: replace the temporary console shell with a Borealis controller-first application shell providing Home, Search, Subscriptions, Library, and Settings navigation; establish touch/controller behavior, SD-card settings/log paths, and app resources.

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
- Native build validation is in progress; M1 is NOT device accepted.
- Hardware checklist: `docs/M1_DEVICE_TEST.md`.

The foundation no-ads text describes the product goal. Neither that screen nor
this interface preview proves ad-free playback; networking and playback remain
unimplemented. Authentication is still an unvalidated technical milestone.
