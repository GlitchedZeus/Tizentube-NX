#pragma once
#include <cstdint>
#include <string_view>

namespace ttnx::playback {
struct MpegTsStreams {
    bool recognized{false};
    bool h264{false};
    bool aac{false};
};
// Inspect MPEG-TS program metadata (PAT/PMT) in a bounded sample, without
// materializing video bytes. This is only evidence of elementary stream types,
// not successful decoding, DRM authorization, or any host permission.
[[nodiscard]] MpegTsStreams inspect_mpeg_ts(std::string_view data);
} // namespace ttnx::playback
