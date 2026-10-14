#if defined(TTNX_M4_RING_STREAM_CB_PROBE) && defined(TTNX_M3_INTEGRATED)
#include "m4_mpv_stream_cb_probe.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
#include "tizentube_nx/playback/native_ring_plan.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/core/content_filter.hpp"
#include "app_storage.hpp"
#include <vector>
#include <type_traits>

namespace ttnx::switch_app {
namespace {
struct OwnedNativeHlsReader {
    std::shared_ptr<LibnxHlsSegmentTransport> transport;
    playback::HlsSegmentReader reader;
    explicit OwnedNativeHlsReader(
        std::shared_ptr<LibnxHlsSegmentTransport> owned)
        :transport(std::move(owned)),reader(*transport,transport.get()) {}
    void cancel() noexcept {transport->request_cancel();}
    void reset_cancel() noexcept {transport->reset_cancel();}
    playback::HlsStreamResult stream(
        std::string_view url,const playback::HlsMediaAdmission& admit,
        const playback::HlsMediaChunk& emit) {
        return reader.stream(url,admit,emit);
    }
};
} // namespace
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
[[nodiscard]] std::shared_ptr<playback::RollingStreamEpoch>
create_native_ring_epoch(const playback::HlsProbePlan& plan,
                         const core::ContentItem& normal_browse_result) {
    // Host-testable, fail-closed admission BEFORE SD and HTTPS services.
    // This also enforces full playlist duration and a <=512KiB URL plan.
    auto accepted=playback::admit_native_ring_plan(plan,normal_browse_result);
    if(!accepted)return {};
    auto transport=std::make_shared<LibnxHlsSegmentTransport>();
    if(!transport->ready())return {};
    auto reader=std::make_shared<OwnedNativeHlsReader>(std::move(transport));
    const std::string path=std::string(ttnx::kAppDirectory)+"/m4_stream_ring.part";
    auto owned=playback::RollingStreamEpoch::with_reader(
        path,std::move(accepted->urls),std::move(reader),
        [](std::string_view url) {
            return playback::is_reviewable_hls_child_url(url);
        },playback::RollingFileStream::kMaximumMedia,
        std::move(accepted->durations_ms));
    return owned
        ? std::shared_ptr<playback::RollingStreamEpoch>(std::move(owned))
        : nullptr;
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
