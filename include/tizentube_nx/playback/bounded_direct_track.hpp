// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tizentube_nx/playback/media_chunks.hpp"
#include "tizentube_nx/playback/playback.hpp"
#include <utility>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ttnx::playback {
// One SOURCE track, one fixed 256-KiB RAM window. No SD file, playback cache
// directory, browser networking, libmpv outbound fetch or logged bearer URL.
// Real MediaChunkReader enforces HTTPS CDN allowlist *before DNS*, a strict
// 206/Content-Range, 1-MiB maximum response and reviewed redirects.
class BoundedDirectTrack final {
public:
    static constexpr std::uint64_t kWindow=256ULL*1024;
    static constexpr std::uint64_t kMaxSource=8ULL*1024*1024*1024;
    static constexpr std::size_t kMaxRead=32768;
    enum class Fault {None, Policy, Http, ChangedResource, InvalidMp4, Cancelled};
    BoundedDirectTrack(MediaChunkReader& reader,std::string url)
        :reader_(reader),url_(std::move(url)) {
        if(url_.empty()||url_.size()>8192||
           !is_reviewable_googlevideo_https_url(url_))
            fault_=Fault::Policy;
    }
    BoundedDirectTrack(const BoundedDirectTrack&)=delete;
    BoundedDirectTrack& operator=(const BoundedDirectTrack&)=delete;
    ~BoundedDirectTrack(){
        cancel();
        std::lock_guard<std::mutex> lock(mu_);
        std::fill(url_.begin(),url_.end(),'\0');url_.clear();
        std::fill(window_.begin(),window_.end(),'\0');window_.clear();
    }
    // Run once on a background setup worker, not the Borealis UI thread.
    // Validates only a top-level MP4 'ftyp'; the codec and sample indexes
    // remain libmpv/FFmpeg's job. Fail closed on malformed first response.
    [[nodiscard]] bool prime() {
        char ftyp[12]{};
        if(read_at(0,ftyp,sizeof(ftyp))!=static_cast<std::int64_t>(sizeof(ftyp)))
            return false;
        if(std::memcmp(ftyp+4,"ftyp",4)!=0 ||
           (static_cast<unsigned char>(ftyp[0])==0 &&
            static_cast<unsigned char>(ftyp[1])==0 &&
            static_cast<unsigned char>(ftyp[2])==0 &&
            static_cast<unsigned char>(ftyp[3])<16)) {
            std::lock_guard<std::mutex> lock(mu_);
            fault_=Fault::InvalidMp4;
            return false;
        }
        return true;
    }
    // Decoder read() runs on mpv worker threads. Each request fetches at
    // most 256 KiB, and the returned data is capped to a single 32-KiB
    // callback read. Seeking only changes logical cursor in the mpv cookie.
    [[nodiscard]] std::int64_t read_at(std::uint64_t offset,char* output,
                                      std::uint64_t wanted) {
        if(!output||wanted==0||wanted>kMaxRead)return -1;
        if(cancelled_.load(std::memory_order_acquire))return -1;
        std::lock_guard<std::mutex> lock(mu_);
        if(fault_!=Fault::None || cancelled_.load(std::memory_order_acquire))
            return -1;
        if(size_ && offset>=size_)return 0;
        if(window_.empty() || offset<window_first_ ||
           offset>=window_first_+window_.size()) {
            const std::uint64_t first=(offset/kWindow)*kWindow;
            const auto range=next_media_range(first,size_,kWindow);
            if(!range) {fault_=Fault::Http;return -1;}
            auto bytes=reader_.read(url_,*range);
            if(cancelled_.load(std::memory_order_acquire)) {
                fault_=Fault::Cancelled;return -1;
            }
            if(!bytes.ok()) {fault_=Fault::Http;return -1;}
            if(bytes.resource_size<16 || bytes.resource_size>kMaxSource ||
               (size_ && size_!=bytes.resource_size)) {
                fault_=Fault::ChangedResource;return -1;
            }
            if(bytes.bytes.empty() || bytes.bytes.size()>kWindow ||
               offset<first || offset>=first+bytes.bytes.size()) {
                fault_=Fault::Http;return -1;
            }
            size_=bytes.resource_size;
            window_first_=first;
            window_.assign(bytes.bytes.begin(),bytes.bytes.end());
            ++ranges_fetched_;
        }
        const auto in_window=offset-window_first_;
        const auto n=std::min<std::uint64_t>({
            wanted,static_cast<std::uint64_t>(window_.size())-in_window,
            size_-offset,kMaxRead});
        std::memcpy(output,window_.data()+in_window,static_cast<std::size_t>(n));
        return static_cast<std::int64_t>(n);
    }
    [[nodiscard]] std::int64_t size()const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return size_?static_cast<std::int64_t>(size_):-1;
    }
    [[nodiscard]] Fault fault()const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return fault_;
    }
    [[nodiscard]] std::uint64_t fetched_ranges()const noexcept {
        std::lock_guard<std::mutex> lock(mu_);return ranges_fetched_;
    }
    [[nodiscard]] std::uint64_t retained_bytes()const noexcept {
        std::lock_guard<std::mutex> lock(mu_);return window_.size();
    }
    void cancel()noexcept {cancelled_.store(true,std::memory_order_release);}
private:
    MediaChunkReader& reader_;
    std::string url_; // ephemeral; never diagnostics or SD
    mutable std::mutex mu_;
    std::atomic<bool> cancelled_{false};
    std::vector<char> window_;
    std::uint64_t size_{0},window_first_{0},ranges_fetched_{0};
    Fault fault_{Fault::None};
};
} // namespace ttnx::playback
