#pragma once
#include <string>
#include <cstdint>
#include <memory>
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
namespace ttnx::playback {class RollingStreamEpoch;}
#endif
#if defined(TTNX_M4_DIRECT_AV_PROBE)
namespace ttnx::switch_app {class NativeDirectAvSession;}
#endif
#ifdef TTNX_M3_INTEGRATED
namespace ttnx::switch_app {

// UI-thread only. Opens *only* the fixed, atomically completed media cache.
// Never accepts a signed URL or user-supplied path. The launcher must first
// pass native extraction, range transport and cache completion gates.
void open_m3_completed_media(const std::string& video_id={},
                          std::uint32_t resume_seconds=0);
void open_m3_completed_hls(const std::string& video_id={},
                          std::uint32_t resume_seconds=0);
void open_m3_completed_adaptive(const std::string& video_id={},
                          std::uint32_t resume_seconds=0);
#if defined(TTNX_M4_ROLLING_STREAM) && defined(TTNX_M4_PLAYER_PROBE)
// Local growing HLS only. No menu call until worker lifetime proven.
void open_m4_local_hls_probe();
#endif
#if defined(TTNX_M4_RING_STREAM_CB_PROBE)
// Explicit opt-in only, no Search/main-menu route until NRO + hardware pass.
// Producer session must have reported a verified playable TS prefix.
void open_m4_native_ring_candidate(std::shared_ptr<playback::RollingStreamEpoch> epoch);
#endif
#if defined(TTNX_M4_DIRECT_AV_PROBE) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
void open_m4_native_direct_candidate(std::shared_ptr<NativeDirectAvSession> stream,
                                     const std::string& video_id);
#endif
void tick_m3_player();

} // namespace ttnx::switch_app
#endif
