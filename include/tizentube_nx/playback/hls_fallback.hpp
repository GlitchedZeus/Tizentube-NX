#pragma once
#include "tizentube_nx/playback/hls_probe.hpp"
namespace ttnx::playback {
// A fallback is another independently validated, local native-media route.
// Never retry guest restrictions, missing identity or an unsafe origin.
inline bool permits_muxed_mp4_fallback(HlsProbeError source) noexcept {
    switch(source) {
        case HlsProbeError::ManifestRejected:
        case HlsProbeError::ManifestMimeRejected:
        case HlsProbeError::ManifestLengthRejected:
        case HlsProbeError::ManifestBodyRejected:
        case HlsProbeError::MasterRejected:
        case HlsProbeError::VariantUnavailable:
        case HlsProbeError::VideoSegmentsRejected:
        case HlsProbeError::AudioSegmentsRejected:
            return true;
        case HlsProbeError::None:
        case HlsProbeError::SourceUnavailable:
        case HlsProbeError::ManifestTransport:
        case HlsProbeError::ManifestStatus:
        case HlsProbeError::InvalidDuration:
        case HlsProbeError::GuestTransport:
        case HlsProbeError::GuestHttpStatus:
        case HlsProbeError::GuestPlayabilityRejected:
            return false;
    }
    return false;
}
} // namespace ttnx::playback
