#include "tizentube_nx/playback/rolling_file_stream.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <thread>
using ttnx::playback::RollingFileStream;
namespace fs=std::filesystem;
int main() {
    const auto dir=fs::temp_directory_path()/"ttnx_ring_atomic_test";
    fs::remove_all(dir);fs::create_directory(dir);
    auto ring=RollingFileStream::create((dir/"av.part").string(),65536);
    assert(ring);
    auto reader=ring->open_reader();assert(reader);
    const std::string packet(188*4,'Z');
    assert(ring->begin_segment(packet.size()));
    assert(!ring->release_before(0));
    assert(ring->append_staged(packet)==RollingFileStream::Write::Written);
    assert(ring->stats().written_end==0);
    assert(ring->stats().physical_size<=65536);
    auto pending=std::async(std::launch::async,[&] {
        std::string bytes(packet.size(),'\0');
        const auto got=reader->read(bytes.data(),bytes.size());
        return std::pair<std::int64_t,std::string>{got,std::move(bytes)};
    });
    assert(pending.wait_for(std::chrono::milliseconds(25))==std::future_status::timeout);
    assert(ring->commit_segment());
    const auto read=pending.get();
    assert(read.first==static_cast<std::int64_t>(packet.size()) && read.second==packet);
    reader.reset();
    assert(ring->release_before(packet.size()));
    assert(ring->begin_segment(packet.size()));
    assert(ring->append_staged(packet)==RollingFileStream::Write::Written);
    ring->abort_segment();
    assert(!ring->staging());
    assert(ring->stats().written_end==packet.size());
    ring->finish();ring.reset();
    assert(!fs::exists(dir/"av.part"));
    auto cancelled=RollingFileStream::create((dir/"cancel.part").string(),65536);
    assert(cancelled && cancelled->begin_segment(packet.size()));
    assert(cancelled->append_staged(packet)==RollingFileStream::Write::Written);
    cancelled->cancel();
    assert(!cancelled->commit_segment());
    assert(cancelled->stats().written_end==0);
    cancelled.reset();assert(!fs::exists(dir/"cancel.part"));
    fs::remove_all(dir);
}
