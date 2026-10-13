// SPDX-License-Identifier: GPL-3.0-or-later
// Concept adapted from Switch-NewPipe's mpv switchcache:// bridge:
// https://github.com/mirusu400/switch-newpipe/blob/1cd590e212807a200ca5d90043051ce0f8cdbfb3/src/switch/switch_player.cpp
// Copyright retained by upstream; original implementation here for bounded
// ring storage, decoder leases, backpressure and short-lived media deletion.
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ttnx::playback {
class RollingFileStream final : public std::enable_shared_from_this<RollingFileStream> {
public:
    // NRO+app state+media combined <=100MB. Separate 128 KiB metadata reserve.
    static constexpr std::uint64_t kMaximumMedia = 24ULL * 1024 * 1024 - 128ULL * 1024;
    static constexpr std::size_t kMaximumChunk = 32 * 1024;
    enum class Write { Written, Backpressure, Invalid, Closed, IoError };
    struct Stats {
        std::uint64_t retained_start{0}, written_end{0}, physical_size{0};
        std::uint32_t readers{0};
        bool cancelled{false}, complete{false}, failed{false};
    };
    // Local path MUST be supplied from an independently validated fixed
    // app-owned media directory. O_EXCL/O_NOFOLLOW refuses overwrites/links.
    static std::shared_ptr<RollingFileStream> create(std::string path,
                                                    std::uint64_t max_bytes=kMaximumMedia) {
        if(path.empty() || max_bytes < 376 || max_bytes > kMaximumMedia) return {};
        int flags=O_CREAT|O_EXCL|O_RDWR;
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        const int fd=::open(path.c_str(),flags,0600);
        if(fd<0) return {};
        struct stat st{};
        if(::fstat(fd,&st)!=0 || !S_ISREG(st.st_mode)) {
            (void)::close(fd); (void)::unlink(path.c_str()); return {};
        }
        return std::shared_ptr<RollingFileStream>(new RollingFileStream(std::move(path),fd,max_bytes));
    }
    RollingFileStream(const RollingFileStream&)=delete;
    RollingFileStream& operator=(const RollingFileStream&)=delete;
    ~RollingFileStream() {
        cancel();
        if(fd_>=0)(void)::close(fd_);
        // Destruction happens after all reader cookies release shared_ptr.
        // A crashed process is cleaned by the app's startup orphan sweep.
        if(!path_.empty())(void)::unlink(path_.c_str());
    }
    [[nodiscard]] Write append(std::string_view bytes) {
        std::lock_guard<std::mutex> lock(mu_);
        if(cancelled_ || finished_ || failed_ || staging_)return Write::Closed;
        if(bytes.empty() || bytes.size()>kMaximumChunk)return Write::Invalid;
        if(bytes.size()>capacity_-(end_-begin_))return Write::Backpressure;
        if(!write_at_locked(end_,bytes))return Write::IoError;
        end_+=bytes.size();
        high_water_=std::max(high_water_,end_-begin_);
        physical_size_=std::max<std::uint64_t>(physical_size_,std::min(end_,capacity_));
        cv_.notify_all();
        return Write::Written;
    }
    // Staged network segment writes are invisible to mpv until the HTTPS
    // length, transport packets and H264/AAC streams are ALL verified.
    // This uses the SAME bounded SD ring: no second 16-MiB temporary file.
    [[nodiscard]] bool begin_segment(std::uint64_t declared_bytes) {
        std::lock_guard<std::mutex> lock(mu_);
        if(cancelled_ || finished_ || failed_ || staging_ ||
           declared_bytes < 376 || declared_bytes > 16ULL*1024*1024 ||
           declared_bytes > capacity_ - (end_ - begin_))return false;
        staging_=true;
        staging_expected_=declared_bytes;
        staged_end_=end_;
        return true;
    }
    [[nodiscard]] Write append_staged(std::string_view bytes) {
        std::lock_guard<std::mutex> lock(mu_);
        if(cancelled_ || finished_ || failed_ || !staging_)return Write::Closed;
        if(bytes.empty() || bytes.size()>kMaximumChunk ||
           bytes.size()>staging_expected_-(staged_end_-end_))return Write::Invalid;
        if(!write_at_locked(staged_end_,bytes))return Write::IoError;
        staged_end_+=bytes.size();
        physical_size_=std::max<std::uint64_t>(physical_size_,
                              std::min(staged_end_,capacity_));
        return Write::Written;
    }
    [[nodiscard]] bool commit_segment() {
        std::lock_guard<std::mutex> lock(mu_);
        if(!staging_ || cancelled_ || finished_ || failed_ ||
           staged_end_-end_!=staging_expected_)return false;
        if(::fsync(fd_)!=0){failed_=true;cv_.notify_all();return false;}
        end_=staged_end_;
        staging_=false;
        staging_expected_=0;
        high_water_=std::max(high_water_,end_-begin_);
        cv_.notify_all();
        return true;
    }
    void abort_segment() noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        staged_end_=end_;
        staging_=false;
        staging_expected_=0;
        cv_.notify_all();
    }
    [[nodiscard]] bool staging()const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return staging_;
    }
    class Reader final {
    public:
        Reader(const Reader&)=delete;
        Reader& operator=(const Reader&)=delete;
        ~Reader(){close();}
        // >0 bytes read; 0 is final EOF; -1 cancelled, failed or invalid.
        std::int64_t read(char* out,std::uint64_t requested) {
            if(!out || !requested)return -1;
            auto owner=snapshot_owner();
            if(!owner)return -1;
            std::unique_lock<std::mutex> lock(owner->mu_);
            owner->cv_.wait(lock,[&]{return closed_ || owner->cancelled_ || owner->failed_ ||
                pos_<owner->end_ || owner->finished_;});
            if(closed_ || owner->cancelled_ || owner->failed_ || pos_<owner->begin_)return -1;
            if(pos_>=owner->end_)return 0;
            const auto count=std::min<std::uint64_t>({requested,
                owner->end_-pos_,kMaximumChunk,
                owner->capacity_-(pos_%owner->capacity_)});
            std::size_t got=0;
            while(got<count) {
                const auto n=::pread(owner->fd_,out+got,count-got,
                    static_cast<off_t>((pos_%owner->capacity_)+got));
                if(n<0 && errno==EINTR)continue;
                if(n<=0){owner->failed_=true;owner->cv_.notify_all();return -1;}
                got+=static_cast<std::size_t>(n);
            }
            pos_+=got;
            return static_cast<std::int64_t>(got);
        }
        // Unlike SEEK_END, the absolute target must ALREADY be buffered.
        [[nodiscard]] std::int64_t seek(std::int64_t absolute) {
            auto owner=snapshot_owner();
            if(!owner || absolute<0)return -1;
            std::lock_guard<std::mutex> lock(owner->mu_);
            const auto at=static_cast<std::uint64_t>(absolute);
            if(closed_ || owner->cancelled_ || owner->failed_ ||
               at<owner->begin_ || at>owner->end_)return -1;
            pos_=at;
            return absolute;
        }
        // mpv cancel_fn can arrive on another thread. Wake blocked read().
        void cancel() {
            // libmpv may call cancel_fn on a different thread than read_fn
            // or close_fn. Never race a shared_ptr copy with close's move.
            auto owner=snapshot_owner();
            if(owner)owner->cancel();
        }
        [[nodiscard]] std::uint64_t position()const noexcept {
            return pos_.load(std::memory_order_acquire);
        }
        void close() {
            std::shared_ptr<RollingFileStream> owner;
            {
                std::lock_guard<std::mutex> state_lock(state_mu_);
                owner=std::move(owner_);
            }
            if(owner){
                std::lock_guard<std::mutex> lock(owner->mu_);
                closed_=true;
                if(owner->readers_)--owner->readers_;
                owner->cv_.notify_all();
            }
        }
    private:
        friend class RollingFileStream;
        explicit Reader(std::shared_ptr<RollingFileStream> owner,std::uint64_t start)
            : owner_(std::move(owner)),pos_(start){}
        // Lock order: state_mu_ is released before acquiring owner->mu_.
        // Both close() and cancel() honor this order; a blocked read can
        // always be woken without self-deadlock or shared_ptr data races.
        [[nodiscard]] std::shared_ptr<RollingFileStream> snapshot_owner()const {
            std::lock_guard<std::mutex> state_lock(state_mu_);
            return owner_;
        }
        mutable std::mutex state_mu_;
        std::shared_ptr<RollingFileStream> owner_;
        std::atomic<std::uint64_t> pos_{0};
        bool closed_{false};
    };
    [[nodiscard]] std::unique_ptr<Reader> open_reader(std::uint64_t at=0) {
        std::lock_guard<std::mutex> lock(mu_);
        if(cancelled_ || failed_ || at<begin_ || at>end_ || readers_==UINT32_MAX)return {};
        ++readers_;
        return std::unique_ptr<Reader>(new Reader(shared_from_this(),at));
    }
    // Requires mpv_stream_cb_info.close_fn completion: NEVER infer from
    // playhead/timestamp that a demuxer released its file descriptors.
    [[nodiscard]] bool release_before(std::uint64_t absolute) {
        std::lock_guard<std::mutex> lock(mu_);
        if(readers_ || staging_ || cancelled_ || failed_ || absolute<begin_ || absolute>end_)return false;
        begin_=absolute;
        cv_.notify_all();
        return true;
    }
    void finish(bool success=true) {
        std::lock_guard<std::mutex> lock(mu_);
        finished_=true;
        failed_=failed_||!success||staging_;
        staged_end_=end_;staging_=false;staging_expected_=0;
        cv_.notify_all();
    }
    void cancel() {
        std::lock_guard<std::mutex> lock(mu_);
        cancelled_=true;
        staged_end_=end_;staging_=false;staging_expected_=0;
        cv_.notify_all();
    }
    [[nodiscard]] Stats stats()const {
        std::lock_guard<std::mutex> lock(mu_);
        return {begin_,end_,physical_size_,readers_,cancelled_,finished_,failed_};
    }
    [[nodiscard]] std::uint64_t peak_bytes()const {
        std::lock_guard<std::mutex> lock(mu_);
        return high_water_;
    }
    [[nodiscard]] std::uint64_t capacity_bytes()const noexcept {return capacity_;}
private:
    RollingFileStream(std::string path,int fd,std::uint64_t max)
        :path_(std::move(path)),fd_(fd),capacity_(max){}
    [[nodiscard]] bool write_at_locked(std::uint64_t absolute,std::string_view bytes) {
        std::size_t at=0;
        while(at<bytes.size()) {
            const auto slot=(absolute+at)%capacity_;
            const auto count=std::min<std::size_t>(bytes.size()-at,capacity_-slot);
            std::size_t progress=0;
            while(progress<count) {
                const auto n=::pwrite(fd_,bytes.data()+at+progress,count-progress,
                                      static_cast<off_t>(slot+progress));
                if(n<0 && errno==EINTR)continue;
                if(n<=0) {failed_=true;cv_.notify_all();return false;}
                progress+=static_cast<std::size_t>(n);
            }
            at+=count;
        }
        return true;
    }
    std::string path_;
    int fd_{-1};
    std::uint64_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::uint64_t begin_{0},end_{0},staged_end_{0},staging_expected_{0},
                  physical_size_{0},high_water_{0};
    std::uint32_t readers_{0};
    bool cancelled_{false},finished_{false},failed_{false},staging_{false};
};
} // namespace ttnx::playback
