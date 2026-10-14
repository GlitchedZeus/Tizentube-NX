// Concurrent mpv cancel_fn/close_fn/read_fn lease regression.
// The real RollingFileStream class is compiled, not a synthetic adapter.
#include "tizentube_nx/playback/rolling_file_stream.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
using ttnx::playback::RollingFileStream;
namespace fs=std::filesystem;
int main() {
    const auto root=fs::temp_directory_path()/"ttnx-reader-race";
    fs::remove_all(root);fs::create_directories(root);
    for(int iteration=0;iteration<256;++iteration) {
        const auto path=(root/("stream-"+std::to_string(iteration)+".part")).string();
        auto ring=RollingFileStream::create(path,65536);
        assert(ring);
        auto reader=ring->open_reader();assert(reader);
        std::atomic<bool> reading{false};
        std::atomic<std::int64_t> read_result{-99};
        std::thread blocked([&]{
            char b{}; reading.store(true,std::memory_order_release);
            read_result.store(reader->read(&b,1),std::memory_order_release);
        });
        while(!reading.load(std::memory_order_acquire))std::this_thread::yield();
        std::thread cancel([&]{
            for(int i=0;i<8;++i)reader->cancel();
        });
        std::thread close([&]{
            reader->close();reader->close();
        });
        cancel.join();close.join();blocked.join();
        assert(read_result.load(std::memory_order_acquire)==-1);
        assert(!ring->stats().readers);
        assert(reader->seek(0)==-1);
        assert(reader->position()==0);
        reader.reset();ring.reset();
        assert(!fs::exists(path));
    }
    // Position snapshots are independent of concurrent decoder cursor writes.
    const auto path=(root/"position.part").string();
    auto ring=RollingFileStream::create(path,65536);assert(ring);
    auto reader=ring->open_reader();assert(reader);
    assert(ring->append(std::string(32768,'a'))==RollingFileStream::Write::Written);
    std::atomic<bool> done{false};
    std::thread consumer([&]{
        char data[1024];
        while(reader->position()<32768) {
            const auto n=reader->read(data,sizeof(data));
            assert(n>0);
        }
        done.store(true,std::memory_order_release);
    });
    std::uint64_t previous=0;
    while(!done.load(std::memory_order_acquire)) {
        const auto current=reader->position();
        assert(current>=previous && current<=32768);
        previous=current;
    }
    consumer.join();assert(reader->position()==32768);
    reader.reset();ring.reset();assert(!fs::exists(path));
    fs::remove_all(root);
}
