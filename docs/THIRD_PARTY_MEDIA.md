# Third-party media protocol attribution

## Switch-NewPipe UMP framing code

TizenTube NX is published under **GPL-3.0-or-later**. The original
[Switch-NewPipe](https://github.com/mirusu400/switch-newpipe) is licensed under
**GPL-3.0**. Its protocol-framing helpers provide a useful, hardware-researched
starting point; they are not a complete playable YouTube extractor.

Original reference revision:
`a54b009f6d046a8ea7585e86c3c7ed41c4a57984`.

Adapted files:

- `src/core/ump_wire.cpp`, derived from upstream `src/common/ump.cpp`.
- `include/tizentube_nx/playback/ump_wire.hpp`, derived from upstream
  `include/newpipe/ump.hpp`.

Changes as of 2026-10-11: TizenTube NX namespace/header integration and
prominent source/SPDX attribution comments. TizenTube NX separately implements
its own strict transport policy, `ump_safety.cpp`, `ump_transfer.cpp`,
independent bounded source routing, tests, and native Horizon SSL adapter.

The imported upstream UMP code retains its **GPL-3.0-only** notice. In any
combined distribution, respect the GPLv3 obligations and preserve copyright,
provenance and source availability; do not silently relicense the imported
source as "or later." No original license/copyright notice should be removed
from copied upstream code. TizenTube NX's existing top-level GPL3-or-later
header is not authorization to remove upstream license conditions.

Documentation and architecture research sources:

- [Switch-NewPipe playback design](https://github.com/mirusu400/switch-newpipe/blob/main/docs/playback.md)
- [July 2026 Android VR UMP / HLS findings](https://github.com/mirusu400/switch-newpipe/blob/main/docs/potoken-720p-findings.md)
- [yt-dlp PO Token guide](https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide)
- [yt-dlp EJS runtime guide](https://github.com/yt-dlp/yt-dlp/wiki/EJS)

**Hardware status:** The Switch-NewPipe reports are upstream historical
evidence, not TizenTube NX hardware acceptance. Public YouTube protocols change;
even upstream July 2026 successes can be incompatible with October 2026
playability/token enforcement. This imported parser is NOT authorization for
arbitrary media network calls, bypassing access restrictions, or auto-following
redirects. TizenTube's physical offline libmpv checkpoint remains separate.
