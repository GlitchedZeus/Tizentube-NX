#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace ttnx::playback {
// Incremental, constant-memory MP4 top-level structure verification.
// This proves bounded complete container framing, NOT that AAC frames are
// decodable, legal, or synchronized to video. mpv remains the decoder.
class AacMp4Structure {
public:
    void reset() noexcept { *this=AacMp4Structure{}; }
    bool begin(std::uint64_t length,
               std::uint64_t limit=128ULL*1024*1024) noexcept {
        reset();
        if(length<1024 || !limit || limit>512ULL*1024*1024 ||
           length>limit)return false;
        total_=length;
        valid_=true;
        return true;
    }
    bool append(std::string_view input) noexcept {
        if(!valid_ || input.size()>total_-received_)return invalid();
        while(!input.empty()) {
            if(payload_left_) {
                const auto n=std::min<std::uint64_t>(payload_left_,input.size());
                payload_left_-=n;
                received_+=n;
                input.remove_prefix(static_cast<std::size_t>(n));
                continue;
            }
            while(header_used_<header_needed_ && !input.empty()) {
                header_[header_used_++]=static_cast<unsigned char>(input.front());
                input.remove_prefix(1);
                ++received_;
            }
            if(header_used_<header_needed_)break;
            const auto length32=read32(header_);
            if(header_needed_==8 && length32==1) {
                header_needed_=16;
                continue;
            }
            const std::uint64_t start=received_-header_needed_;
            const auto size=length32==1?read64(header_+8):
                 length32?static_cast<std::uint64_t>(length32):total_-start;
            const std::string_view type(
                reinterpret_cast<const char*>(header_+4),4);
            // Length zero extends to EOF only for the final media-data box.
            if((length32==0 && type!="mdat") ||
               size<header_needed_ || size>total_-start ||
               (++boxes_>512))return invalid();
            if(boxes_==1 && type!="ftyp")return invalid();
            if(type=="ftyp") {
                if(ftyp_ || size<16)return invalid();
                ftyp_=true;
            } else if(type=="moov") {
                if(moov_ || size<16)return invalid();
                moov_=true;
            } else if(type=="mdat") {
                if(size<=header_needed_)return invalid();
                mdat_=true;
            }
            payload_left_=size-header_needed_;
            header_used_=0;
            header_needed_=8;
        }
        return valid_;
    }
    [[nodiscard]] bool complete() const noexcept {
        return valid_ && ftyp_ && moov_ && mdat_ &&
               received_==total_ && payload_left_==0 && header_used_==0;
    }
private:
    static std::uint32_t read32(const unsigned char* p) noexcept {
        return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
               (std::uint32_t(p[2])<<8)|std::uint32_t(p[3]);
    }
    static std::uint64_t read64(const unsigned char* p) noexcept {
        return (std::uint64_t(read32(p))<<32)|read32(p+4);
    }
    bool invalid() noexcept {valid_=false;return false;}
    bool valid_{false},ftyp_{false},moov_{false},mdat_{false};
    std::uint64_t total_{0},received_{0},payload_left_{0};
    unsigned boxes_{0},header_used_{0},header_needed_{8};
    unsigned char header_[16]{};
};
} // namespace ttnx::playback
