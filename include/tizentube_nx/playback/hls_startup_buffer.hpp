#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ttnx::playback {
// Preparation-only state for future early HLS playback. Does not publish
// partial media or grant libmpv any network/file access.
class HlsStartupBuffer {
public:
    static constexpr std::uint64_t kTargetMs=12000;
    static constexpr std::uint64_t kMaxPrefixBytes=48ULL*1024*1024;
    static constexpr std::size_t kMaxPrefixSegments=6;
    bool configure(const std::vector<std::uint64_t>& durations) noexcept {
        reset();
        if(durations.empty() || durations.size()>4096)return false;
        std::uint64_t sum=0;
        for(auto duration:durations) {
            if(!duration || duration>60000)return false;
            if(prefix_count_<kMaxPrefixSegments) {
                ++prefix_count_;
                sum+=duration;
                if(sum>=kTargetMs)break;
            }
        }
        target_ms_=sum;
        configured_=true;
        return true;
    }
    void mark_video_codec(bool verified) noexcept {video_verified_=verified;}
    void mark_complete_audio(bool verified) noexcept {audio_verified_=verified;}
    bool add_video(std::size_t index,std::uint64_t duration,
                   std::size_t bytes) noexcept {
        if(!configured_ || index!=next_index_ || !duration ||
           duration>60000 || !bytes || bytes>16ULL*1024*1024)
            return false;
        ++next_index_;
        if(index<prefix_count_) {
            if(bytes>kMaxPrefixBytes-prefix_bytes_)return false;
            prefix_bytes_+=bytes;
            prefix_ms_+=duration;
        }
        return true;
    }
    [[nodiscard]] bool prepared() const noexcept {
        return configured_ && audio_verified_ && video_verified_ &&
               next_index_>=prefix_count_ && prefix_ms_>=target_ms_ &&
               prefix_bytes_<=kMaxPrefixBytes;
    }
    [[nodiscard]] std::size_t required_segments() const noexcept {
        return prefix_count_;
    }
    [[nodiscard]] std::uint64_t prefix_ms() const noexcept {
        return prefix_ms_;
    }
    void reset() noexcept {
        configured_=audio_verified_=video_verified_=false;
        next_index_=prefix_count_=0;
        prefix_ms_=prefix_bytes_=target_ms_=0;
    }
private:
    bool configured_{false},audio_verified_{false},video_verified_{false};
    std::size_t next_index_{0},prefix_count_{0};
    std::uint64_t target_ms_{0},prefix_ms_{0},prefix_bytes_{0};
};
}
