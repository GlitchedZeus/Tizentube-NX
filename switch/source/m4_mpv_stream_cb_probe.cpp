#if defined(TTNX_M4_RING_STREAM_CB_PROBE) && defined(TTNX_M3_INTEGRATED)
#include "m4_mpv_stream_cb_probe.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
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
    // A valid 11-character video ID is NOT proof of non-Shorts origin.
    // The caller must preserve the already-filtered normal-video identity
    // from Search/Browse and match it to the HLS probe before any SD/HTTPS.
    if(normal_browse_result.kind!=core::ContentKind::Video ||
       normal_browse_result.id!=plan.source.video_id ||
       core::should_hide(normal_browse_result))return {};
    // All other inputs originate from the reviewed M4 source/manifest probe.
    // Reject separate AAC, fMP4 init fragments, Shorts and over-720p
    // before opening SD files or doing another CDN request.
    if(!core::is_valid_video_id(plan.source.video_id) ||
       plan.selected.height==0 || plan.selected.height>720 ||
       plan.selected.fps==0 || plan.selected.fps>60 ||
       plan.audio_segments || !plan.selected.audio_playlist_url.empty() ||
       !plan.source.external_aac_url.empty() ||
       plan.video_segments.init_segment_url ||
       plan.video_segments.segments.empty() ||
       plan.video_segments.segments.size()>4096 ||
       plan.source.duration_ms==0) return {};
    std::vector<std::string> urls;
    std::vector<std::uint64_t> segment_durations;
    urls.reserve(plan.video_segments.segments.size());
    segment_durations.reserve(plan.video_segments.segments.size());
    std::uint64_t duration=0;
    for(const auto& segment:plan.video_segments.segments) {
        if(!segment.duration_ms || segment.duration_ms>60000 ||
           !playback::is_reviewable_hls_child_url(segment.url) ||
           duration>UINT64_MAX-segment.duration_ms)return {};
        duration+=segment.duration_ms;
        urls.push_back(segment.url);
        segment_durations.push_back(segment.duration_ms);
    }
    const auto mismatch=duration>plan.source.duration_ms
        ?duration-plan.source.duration_ms
        :plan.source.duration_ms-duration;
    if(mismatch>3000)return {};
    auto transport=std::make_shared<LibnxHlsSegmentTransport>();
    if(!transport->ready())return {};
    auto reader=std::make_shared<OwnedNativeHlsReader>(std::move(transport));
    const std::string path=std::string(ttnx::kAppDirectory)+"/m4_stream_ring.part";
    auto owned=playback::RollingStreamEpoch::with_reader(
        path,std::move(urls),std::move(reader),
        [](std::string_view url) {
            return playback::is_reviewable_hls_child_url(url);
        },playback::RollingFileStream::kMaximumMedia,
        std::move(segment_durations));
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
