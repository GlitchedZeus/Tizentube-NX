#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace ttnx::switch_app {

// Owns ONE opt-in M4 HTTP producer thread. The renderer may only read
// thread-safe snapshots; it must never pump a network request on the UI tick.
// The accepted M3 player does not instantiate this class.
class RollingHlsWorker final {
public:
    enum class Step { Fetching, PrefixReady, Waiting, Complete, Failed };
    class Job {
    public:
        virtual ~Job() = default;
        virtual Step fetch_one() = 0;
        // Called on the producer thread, never concurrently with fetch_one.
        // The owner MUST prove each decoder FD below first_index is closed.
        virtual bool retire_before(std::size_t first_index,
                                   std::uint64_t playhead_ms) = 0;
        // Called only after the worker has joined AND libmpv has closed.
        virtual bool cleanup_after_decoder() = 0;
    };
    enum class State { Idle, Fetching, PrefixReady, Waiting, Complete,
                       Failed, StopRequested, Joined, Cleared };
    struct Snapshot {
        State state{State::Idle};
        std::uint64_t published_segments{0};
        std::uint64_t rejected_retirements{0};
        bool prefix_ready{false};
        bool worker_ended{false};
    };

    RollingHlsWorker() = default;
    RollingHlsWorker(const RollingHlsWorker&) = delete;
    RollingHlsWorker& operator=(const RollingHlsWorker&) = delete;
    ~RollingHlsWorker();

    [[nodiscard]] bool start(Job& job);
    [[nodiscard]] Snapshot snapshot() const noexcept;
    // Decoder release must be independently confirmed by the native player.
    // This is one pending message, never an unbounded UI-to-worker queue.
    [[nodiscard]] bool request_retirement(std::size_t first_index,
                                          std::uint64_t playhead_ms,
                                          bool decoder_released);
    void request_stop() noexcept;
    // Can block for an in-flight approved HTTP timeout; do NOT join from
    // the draw/render callback or while holding libmpv internal locks.
    void join_downloads() noexcept;
    // The caller must fully terminate libmpv and release its global lease.
    // Explicitly refuses cleanup while the worker or a decoder is active.
    [[nodiscard]] bool cleanup_after_decoder_closed() noexcept;
private:
    struct Retirement { std::size_t first_index; std::uint64_t playhead_ms; };
    void run() noexcept;
    mutable std::mutex mutex_{};
    std::condition_variable wake_{};
    std::thread worker_{};
    Job* job_{nullptr}; // borrowed; owner must outlive join_downloads()
    Snapshot status_{};
    std::optional<Retirement> pending_{};
    bool stop_{false};
};
} // namespace ttnx::switch_app
