#include "tizentube_nx/playback/rolling_file_stream.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
using ttnx::playback::RollingFileStream;
namespace fs=std::filesystem;
static std::string payload(std::size_t count,std::uint64_t position) {
    std::string out(count,'\0');
    for(std::size_t i=0;i<count;++i)out[i]=static_cast<char>((position+i)*131u%251u);
    return out;
}
int main() {
    const auto dir=fs::temp_directory_path()/"ttnx_ring_stream_test";
    fs::remove_all(dir);fs::create_directories(dir);
    const auto path=(dir/"media.part").string();
    assert(!RollingFileStream::create(path,1));
    auto media=RollingFileStream::create(path,65536);
    assert(media && !RollingFileStream::create(path,65536));
    auto reader=media->open_reader();assert(reader);
    const auto sample=payload(32768,0);
    assert(media->append(sample)==RollingFileStream::Write::Written);
    assert(media->append(sample)==RollingFileStream::Write::Written);
    assert(media->append(sample)==RollingFileStream::Write::Backpressure);
    assert(!media->release_before(32768)); // no active decoder eviction!
    assert(reader->seek(65537)==-1);
    assert(reader->seek(32768)==32768);
    assert(reader->seek(0)==0);
    std::string returned(65536,'\0');
    std::uint64_t n=0;
    while(n<65536){const auto got=reader->read(returned.data()+n,65536-n);
        assert(got>0);n+=static_cast<std::uint64_t>(got);}
    assert(returned==sample+sample);
    reader.reset();
    assert(media->release_before(32768));
    assert(media->append(payload(32768,65536))==RollingFileStream::Write::Written);
    auto second=media->open_reader(32768);assert(second);
    assert(second->seek(0)==-1);
    std::string overlap(65536,'\0');n=0;
    while(n<65536){const auto got=second->read(overlap.data()+n,65536-n);assert(got>0);n+=got;}
    assert(overlap==sample+payload(32768,65536));
    second.reset();
    assert(media->stats().physical_size==65536);
    assert(media->peak_bytes()==65536);
    media->finish();
    auto final=media->open_reader(98304);assert(final);
    char c='x';assert(final->read(&c,1)==0);
    final.reset();media.reset();
    assert(!fs::exists(path)); // zero cached video after last owner
    // Large synthetic stream with fixed physical file size; caller must
    // explicitly close decoder cookie and release each consumed 32-KiB block.
    const auto path2=(dir/"long.part").string();
    auto stream=RollingFileStream::create(path2,65536);
    assert(stream);
    constexpr std::uint64_t total=100ULL*1024*1024;
    for(std::uint64_t at=0;at<total;at+=32768) {
        const auto slice=payload(32768,at);
        assert(stream->append(slice)==RollingFileStream::Write::Written);
        auto active=stream->open_reader(at);assert(active);
        std::string got(32768,'\0');
        std::size_t used=0;
        while(used<got.size()){
            const auto nread=active->read(got.data()+used,got.size()-used);
            assert(nread>0);used+=nread;
        }
        assert(got==slice);
        active.reset();
        assert(stream->release_before(at+32768));
        assert(stream->stats().physical_size<=65536);
    }
    assert(stream->stats().written_end==total);
    assert(stream->peak_bytes()<=32768);
    stream.reset();assert(!fs::exists(path2));
    // Producer's cancellation immediately wakes a waiting decoder thread.
    const auto path3=(dir/"cancel.part").string();
    auto waiting=RollingFileStream::create(path3,65536);
    auto pending=waiting->open_reader();assert(pending);
    std::atomic<bool> began{false};
    std::atomic<std::int64_t> result{99};
    std::thread thread([&]{char item;began=true;result=pending->read(&item,1);});
    while(!began.load())std::this_thread::yield();
    waiting->cancel();thread.join();assert(result==-1);
    pending.reset();waiting.reset();assert(!fs::exists(path3));
    // Reject symlink injection and do not touch the symlink's target.
    const auto target=dir/"original.txt";
    {int fd=::open(target.c_str(),O_CREAT|O_WRONLY|O_TRUNC,0600);assert(fd>=0);
        assert(::write(fd,"SAFE",4)==4);::close(fd);}
    const auto attack=dir/"attack.part";
    fs::create_symlink(target,attack);
    assert(!RollingFileStream::create(attack.string(),65536));
    assert(fs::file_size(target)==4);
    fs::remove_all(dir);
}
