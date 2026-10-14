# Withdrawn Google OAuth / YouTube account linking proposal

**NOT A PRODUCT REQUIREMENT. NOT AN IMPLEMENTATION TARGET.**

On **October 12, 2026**, the TizenTube NX product direction was
explicitly confirmed as **no Google profiles/accounts**, while still
providing local video history and a familiar Library. Consequently,
the older optional post-v1 Google OAuth/device-code, QR sign-in,
refresh-token persistence, private account import, and sync proposals
were **cancelled** and removed from the roadmap.

Do not implement or expose Google sign-in, OAuth callbacks, credential
collection, cookies, private-profile synchronization, account linking,
"Reconnect YouTube", or an equivalent fallback under this archival file.

For the current approved behavior, see:

- `docs/PROFILES_AND_IMPORT.md` — local watch history, resume, Watch Later,
  followed channels, favorites and playlists with bounded metadata.
- `docs/M4_SD_STREAMING_REQUIREMENTS.md` — low-RAM SD-backed streaming,
  approximately 25 MB disposable cache, 100 MB combined app ceiling.
- `docs/PRODUCT_PHILOSOPHY.md` — ad-free, Shorts-free, clean-link UX.

This archived filename remains only to make the reversal discoverable to
developers following outdated internal links.
