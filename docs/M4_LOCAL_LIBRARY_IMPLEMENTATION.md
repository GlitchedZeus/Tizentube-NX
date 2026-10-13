# Local Library — M4 native implementation checkpoint

This engineering tranche implements the **first functioning local Library**;
the previous feature was only a roadmap/placeholder.

## Production code

- `include/tizentube_nx/library/local_library.hpp`: small C++20
  metadata store, strict canonical 11-character video IDs and 24-character
  UC channel IDs, verified CRC32-like FNV-1a integrity footer (non-security
  checksum), versioned file format, strict bounded parser and FAT-compatible
  backup/rename publication.
- The on-disk file is `sdmc:/switch/TizenTube-NX/library.txt`.
  `.tmp` and `.bak` are app-owned only and kept bounded. Files are
  at most 65,536 bytes each (196,608 bytes for three possible generations).
  Entire Library is therefore substantially below the previously proposed
  2,000,000-byte metadata allotment and counts within the 100 MB app ceiling.
- Latest per-video watch state is deduplicated and bounded to
  **1,000 history records**; Watch Later **1,000 IDs**; Favorites **400**;
  locally followed channels **100**. When combined collections would exceed
  the 65,536-byte on-disk file ceiling, saving fails rather than exceeding
  the quota. Collections other than throwaway History are not silently
  evicted. Historical format v1 can be parsed into the new v2 library.
- No thumbnails, video/audio, full YouTube response, cookies, signed URLs,
  analytics, login, account linking or proprietary screenshot assets are
  stored. A favorite/follow only changes **this Switch's local Library**.

## Connected native UX

- **Search:** selecting a normal video exposes **Add to Watch Later**
  and **Add to Favorites**, retaining the accepted Play video (M3)
  and Cancel options. Selecting a channel exposes a local
  **Follow on this Switch** control alongside Open channel.
- **Library:** browse up to twenty recent History, Watch Later and
  Favorites IDs per group, open an ID through the already-approved
  guest video preparation path, and surface the need for a guest
  connection when offline. The 20-row UI cap prevents thousands of
  simultaneously allocated Borealis rows; it is not a data limit.
- **Following:** show locally followed channel IDs and reopen them
  using the existing native channel-browser route. No Google account
  or "Subscribe on YouTube" side effect exists.
- **Settings:** pause/resume local history recording, and require
  two explicit A presses to clear History (without clearing saved
  Watch Later/Favorites/Following).
- **M3 playback:** a video ID is handed from the accepted Search/media
  preparation flow into the existing `M3Activity`. History is written
  **only after** at least one decoded/rendered video frame AND a
  selected audio track; a failed preparation does not count as watched.
  The native mpv time-pos is saved on first playback, roughly every
  thirty seconds of progress, and on exit/stop. If mpv clears time-pos
  on shutdown, the previous nonzero resume position is preserved.
  The known-good renderer, alpha fix, controls, AAC gate and media
  cleanup remain unchanged.

## Test evidence

Locally executed:
```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 \
  -Iinclude tests/local_library_tests.cpp -o /tmp/ttnx-library && /tmp/ttnx-library
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude tests/local_library_tests.cpp -o /tmp/ttnx-library-san
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/ttnx-library-san
```
Both passed against the exact source bytes committed to GitHub.
Tests exercise invalid IDs, deduplication, 1,000-history retention,
resume positions, history privacy pause and clear, local Favorites and
Following, v2 serialization/checksum, corruption rejection, valid-backup
recovery, further save after corruption, and symlink refusal.
The test is also registered in host CTest and standalone sanitizer CI.

**Still pending:** exact-head GitHub host/switch workflows and physical
Switch hardware. This environment has no devkitA64 checkout/toolchain.
Do not claim the integrated Borealis UI or native player compiled/booted
until those CI/hardware gates pass.

## Remaining high-priority work

1. Physical test of Add to Watch Later/Favorites/Follow, persistence over
   application restarts, and opening saved videos.
2. Verify History records only after visible/audible playback,
   updates resume on stop and after long videos, and stays zero for
   failed downloads; no corrupt SD writes across sleep/exit.
3. Complete real resume-from-position on reopening a history record
   (currently the position is **saved and displayed but not automatically
   applied to the next decoder session**). Implement precise seeking
   without accessing deleted SD cache.
4. Implement UI pagination/recycling for History beyond its first
   twenty visible items, local item removal, local unfollow/unfavorite.
5. Implement original thumbnail rails and fullscreen player overlay from
   the 37 user-provided Switch screenshots; do not ship the accepted
   M3 tester view as a final TV UI.
6. Replace full-media M3 predownload with bounded low-RAM rolling SD
   streaming (~25 MB) and safe decoder segment eviction. Measure the
   **100,000,000 decimal byte** total SD ceiling and RAM with sysmodules.
7. Only after normal/sanitizer CI, native devkitA64 compile, and physical
   picture/audio/seek/sleep tests should an integrated NRO be accepted.
