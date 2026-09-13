# Project status

## Current phase

M0 — Foundation / pre-alpha.

## Implemented

- Pure C++ core library that can be tested without a Switch.
- Canonical video share URLs: `https://www.youtube.com/watch?v=<VIDEO_ID>`.
- Common YouTube watch/youtu.be/embed/live URL parsing with tracking data discarded.
- `/shorts/` URLs rejected by the canonicalizer.
- Hard content filter that always hides `ContentKind::Short` even if a future policy/config accidentally disables the setting.
- Default filters for promoted and shopping content.
- Minimal libnx console app source and devkitPro makefile.
- Host CI workflow.

## Validation

Host CMake build and tests are expected to pass before every commit. The Switch target still needs validation in a devkitPro environment and on Atmosphere hardware.

## Immediate next technical checkpoint

Produce a real `tizentube_nx.nro` foundation build and confirm it boots/returns cleanly on the user's Switch. Then replace the console shell with Borealis.

## Known high-risk area

Account authentication. The product requirement is easy console-style login without cookie-file import. Before building account-dependent UI, validate a sustainable TV/device authorization flow suitable for redistribution and avoid embedding third-party private credentials.
