#pragma once
#ifdef TTNX_M3_INTEGRATED
namespace ttnx::switch_app {

// UI-thread only. Opens *only* the fixed, atomically completed media cache.
// Never accepts a signed URL or user-supplied path. The launcher must first
// pass native extraction, range transport and cache completion gates.
void open_m3_completed_media();
void open_m3_completed_hls();
void open_m3_completed_adaptive();
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
// Local growing HLS only. No menu call until worker lifetime proven.
void open_m4_local_hls_probe();
#endif
void tick_m3_player();

} // namespace ttnx::switch_app
#endif
