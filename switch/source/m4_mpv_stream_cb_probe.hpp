#pragma once
#if defined(TTNX_M4_RING_STREAM_CB_PROBE) && defined(TTNX_M3_INTEGRATED)
#include "tizentube_nx/playback/rolling_mpv_stream_bridge.hpp"
#include "tizentube_nx/playback/hls_segment.hpp"
#include "tizentube_nx/playback/verified_ring_hls_ingest.hpp"
#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#include "tizentube_nx/playback/hls_probe.hpp"
#include "libnx_http_client.hpp"
#include <memory>

namespace ttnx::switch_app {
// Own this object until mpv_terminate_destroy returns. Register before
// mpv_initialize. The default M3 player does not call this experimental API.
[[nodiscard]] std::unique_ptr<playback::RollingMpvStreamBridge>
register_native_ring_callback(
    std::shared_ptr<playback::RollingFileStream> file,mpv_handle* player);
 
// Production-shaped ingress: HlsSegmentReader::stream delegates to the
// approved Horizon SSL media transport; it publishes verified muxed TS to
// the SAME ring whose mpv callback owns the decoder-side read handles.
// Caller supplies one app-owned validated ring root and retains the bridge.
// Own the native SSL adapter, HLS reader, worker and bounded ring together.
[[nodiscard]] std::shared_ptr<playback::RollingStreamEpoch>
create_native_ring_epoch(const playback::HlsProbePlan& plan);

[[nodiscard]] playback::VerifiedRingHlsIngest::Result
ingest_native_ring_segment(
    playback::HlsSegmentReader& reader,
    std::shared_ptr<playback::RollingFileStream> ring,
    std::string_view signed_url);
} // namespace ttnx::switch_app
#endif
