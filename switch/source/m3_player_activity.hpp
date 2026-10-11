#pragma once
#ifdef TTNX_M3_INTEGRATED
namespace ttnx::switch_app {

// UI-thread only. Opens *only* the fixed, atomically completed media cache.
// Never accepts a signed URL or user-supplied path. The launcher must first
// pass native extraction, range transport and cache completion gates.
void open_m3_completed_media();
void open_m3_completed_hls();
void tick_m3_player();

} // namespace ttnx::switch_app
#endif
