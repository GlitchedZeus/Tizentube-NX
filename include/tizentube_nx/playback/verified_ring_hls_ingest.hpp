// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tizentube_nx/playback/rolling_file_stream.hpp"
#include "tizentube_nx/playback/streaming_ts_validator.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

namespace ttnx::playback {
// Invoked from the M4 download worker, never the Borealis render tick.
// Connects the REAL HlsSegmentReader::stream(signed_url,admit,chunk)
// contract to mpv's app-owned local ttnxring://media stream source.
// A segment stays invisible to mpv until full Content-Length and TS
// H264/AAC validation pass. There is no second segment-sized SD file.
class VerifiedRingHlsIngest final {
public:
    enum class Result { Committed, Invalid, Backpressure, Transport, Codec, Disk };
    explicit VerifiedRingHlsIngest(std::shared_ptr<RollingFileStream> ring)
       :ring_(std::move(ring)){}

    template<class Reader>
    [[nodiscard]] Result ingest(Reader& reader,std::string_view signed_url) {
        if(!ring_)return Result::Invalid;
        bool admitted=false, refused_space=false, write_failed=false;
        std::size_t declared=0;
        StreamingTsValidator validator;
        const auto transfer=reader.stream(signed_url,
            [&](std::size_t announced) -> bool {
                if(admitted || announced<376 ||
                   announced>StreamingTsValidator::kSegmentCapBytes)return false;
                declared=announced;
                if(!ring_->begin_segment(announced)) {
                    refused_space=!ring_->stats().failed &&
                        !ring_->stats().cancelled &&
                        ring_->stats().written_end-ring_->stats().retained_start+
                        announced>ring_->capacity_bytes();
                    return false;
                }
                admitted=true;
                return true;
            },[&](std::string_view chunk) -> bool {
                if(!admitted || chunk.empty() || chunk.size()>RollingFileStream::kMaximumChunk ||
                   !validator.push(chunk))return false;
                const auto written=ring_->append_staged(chunk);
                if(written!=RollingFileStream::Write::Written) {
                    write_failed=true;
                    return false;
                }
                return true;
            });
        if(!admitted || !transfer.ok || transfer.bytes!=declared ||
           validator.bytes()!=declared || !validator.finish()) {
            ring_->abort_segment();
            if(refused_space)return Result::Backpressure;
            if(write_failed)return Result::Disk;
            if(transfer.ok && admitted)return Result::Codec;
            return Result::Transport;
        }
        if(!ring_->commit_segment()) {
            ring_->abort_segment();
            return Result::Disk;
        }
        return Result::Committed;
    }
private:
    std::shared_ptr<RollingFileStream> ring_;
};
} // namespace ttnx::playback
