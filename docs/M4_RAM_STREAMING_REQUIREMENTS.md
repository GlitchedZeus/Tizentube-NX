# Superseded M4 RAM-only proposal — DO NOT IMPLEMENT

> **Withdrawn October 12, 2026 at user request.** The user prefers a
> small (~25 MB), disposable SD cache because large RAM buffers would
> interfere with memory they reserve for Switch sysmodules. **The active
> requirements are now in [M4_SD_STREAMING_REQUIREMENTS.md](M4_SD_STREAMING_REQUIREMENTS.md).**
> Do not use this withdrawn document as a design or acceptance specification.

The superseded proposal used bounded RAM as the primary store for downloaded
video/audio chunks and prohibited temporary SD media files. That requirement
was corrected. Implement progressive file-backed streaming with low RAM
overhead, a capped rolling SD cache, immediate cleanup on stop, and a
100,000,000-byte combined SD-use ceiling instead.
