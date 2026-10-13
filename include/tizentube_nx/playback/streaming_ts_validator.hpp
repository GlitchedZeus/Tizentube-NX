#pragma once
#include "tizentube_nx/playback/ts_streams.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ttnx::playback {
// Incremental MPEG-TS integrity+stream-type gate. One 64-KiB startup
// sample only; never allocate memory proportional to downloaded duration.
// A file-backed producer must keep partial data unpublishable until finish().
class StreamingTsValidator final {
public:
    static constexpr std::size_t kPacketBytes=188;
    static constexpr std::size_t kSampleBytes=65536;
    static constexpr std::size_t kSegmentCapBytes=16*1024*1024;
    explicit StreamingTsValidator(std::size_t cap=kSegmentCapBytes) noexcept
        :cap_(cap>kSegmentCapBytes?kSegmentCapBytes:cap){}
    [[nodiscard]] bool push(std::string_view bytes) {
        if(done_ || !good_ || bytes.empty() ||
           bytes.size()>cap_ || written_>cap_-bytes.size()) {
            good_=false;return false;
        }
        // Check the TS sync byte at EVERY 188-byte packet boundary even
        // when network chunks split packets arbitrarily (1-byte chunks too).
        std::size_t i=(kPacketBytes-written_%kPacketBytes)%kPacketBytes;
        for(;i<bytes.size();i+=kPacketBytes) {
            if(static_cast<unsigned char>(bytes[i])!=0x47) {
                good_=false;return false;
            }
        }
        if(sample_.size()<kSampleBytes) {
            sample_.append(bytes.data(),std::min(bytes.size(),kSampleBytes-sample_.size()));
        }
        written_+=bytes.size();return true;
    }
    [[nodiscard]] bool finish() {
        if(done_ || !good_ || written_<kPacketBytes*2 ||
           written_%kPacketBytes!=0) {
            good_=false;return false;
        }
        const auto parsed=inspect_mpeg_ts(sample_);
        done_=true;
        return good_=parsed.recognized && parsed.h264 && parsed.aac;
    }
    [[nodiscard]] bool valid()const noexcept{return done_&&good_;}
    [[nodiscard]] std::size_t bytes()const noexcept{return written_;}
    [[nodiscard]] std::size_t buffered_bytes()const noexcept{return sample_.size();}
private:
    std::size_t cap_{kSegmentCapBytes},written_{0};
    bool good_{true},done_{false};
    std::string sample_{};
};
} // namespace ttnx::playback
