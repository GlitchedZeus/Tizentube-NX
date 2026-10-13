#if defined(TTNX_M4_RING_STREAM_CB_PROBE) && defined(TTNX_M3_INTEGRATED)
#include "m4_mpv_stream_cb_probe.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
#include <type_traits>

namespace ttnx::switch_app {
static_assert(std::is_same_v<decltype(mpv_stream_cb_info{}.read_fn),mpv_stream_cb_read_fn>);
static_assert(std::is_same_v<decltype(mpv_stream_cb_info{}.seek_fn),mpv_stream_cb_seek_fn>);
static_assert(std::is_same_v<decltype(mpv_stream_cb_info{}.cancel_fn),mpv_stream_cb_cancel_fn>);
[[nodiscard]] std::unique_ptr<playback::RollingMpvStreamBridge>
register_native_ring_callback(
    std::shared_ptr<playback::RollingFileStream> file,mpv_handle* player) {
    if(!file||!player)return {};
    auto owned=std::make_unique<playback::RollingMpvStreamBridge>(std::move(file));
    if(owned->register_before_initialize(player)<0)return {};
    // Caller must keep this owner alive through mpv's entire lifetime.
    return owned;
}
[[nodiscard]] playback::VerifiedRingHlsIngest::Result
ingest_native_ring_segment(
    playback::HlsSegmentReader& reader,
    std::shared_ptr<playback::RollingFileStream> ring,
    std::string_view signed_url) {
    using Status=playback::VerifiedRingHlsIngest::Result;
    if(!ring || !playback::is_reviewable_hls_child_url(signed_url))
        return Status::Invalid;
    // All media bytes arrive over the native HlsSegmentReader's separately
    // approved HTTPS transport, not through mpv's own network protocols.
    playback::VerifiedRingHlsIngest staged(std::move(ring));
    return staged.ingest(reader,signed_url);
}
} // namespace ttnx::switch_app
#endif
