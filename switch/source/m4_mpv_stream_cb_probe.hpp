#pragma once
#if defined(TTNX_M4_RING_STREAM_CB_PROBE) && defined(TTNX_M3_INTEGRATED)
#include "tizentube_nx/playback/rolling_mpv_stream_bridge.hpp"
#include <memory>

namespace ttnx::switch_app {
// Own this object until mpv_terminate_destroy returns. Register before
// mpv_initialize. The default M3 player does not call this experimental API.
[[nodiscard]] std::unique_ptr<playback::RollingMpvStreamBridge>
register_native_ring_callback(
    std::shared_ptr<playback::RollingFileStream> file,mpv_handle* player);
} // namespace ttnx::switch_app
#endif
