#include "tizentube_nx/playback/rolling_file_stream.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
using ttnx::playback::RollingFileStream;
namespace fs=std::filesystem;
int main() {
    const auto root=fs::temp_directory_path()/"ttnx-m4-seek-read";
    fs::remove_all(root);
    fs::create_directory(root);
    const auto path=(root/"media.part").string();
    auto ring=RollingFileStream::create(path,131072);
    assert(ring);
    auto pattern=[](std::uint64_t pos) {
        return static_cast<char>((pos*37+9)%251);
    };
    std::string bytes(65536,'\0');
    for(std::uint64_t i=0;i<bytes.size();++i)bytes[i]=pattern(i);
    assert(ring->append(std::string_view(bytes.data(),32768))==
           RollingFileStream::Write::Written);
    assert(ring->append(std::string_view(bytes.data()+32768,32768))==
           RollingFileStream::Write::Written);
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;
    // Shared FD, independent callback cursors: lseek()+read() must remain
    // atomic as a pair even with other consumers and producer activity.
    for(unsigned worker=0;worker<8;++worker) {
        workers.emplace_back([&,worker]{
            auto rd=ring->open_reader();if(!rd){ok=false;return;}
            std::string data(16384,'\0');
            for(unsigned attempt=0;attempt<150;++attempt) {
                const auto at=static_cast<std::int64_t>(
                    (attempt*1223+worker*997)%49152);
                if(rd->seek(at)!=at){ok=false;break;}
                const auto n=rd->read(data.data(),data.size());
                if(n!=static_cast<std::int64_t>(data.size()) ||
                   data!=bytes.substr(static_cast<std::size_t>(at),data.size())) {
                    ok=false;break;
                }
            }
        });
    }
    for(auto& t:workers)t.join();
    assert(ok);
    assert(ring->stats().readers==0);

    // The new portable producer write() path must not corrupt the shared FD
    // cursor of a read cookie. Two concurrent readers keep seeking while
    // small, validated staging writes target the next logical media region.
    auto span=ring->open_reader(0);assert(span);
    assert(span->seek(2000)==2000);
    assert(ring->begin_segment(4096));
    std::string staged(4096,'q');
    assert(ring->append_staged(std::string_view(staged.data(),2048))==
           RollingFileStream::Write::Written);
    assert(ring->append_staged(std::string_view(staged.data()+2048,2048))==
           RollingFileStream::Write::Written);
    // Still unpublished: the active decoder cannot observe staged media.
    assert(ring->stats().written_end==65536);
    std::string peek(1024,'\0');
    assert(span->read(peek.data(),peek.size())==1024);
    assert(peek==bytes.substr(2000,peek.size()));
    assert(ring->commit_segment());
    span.reset();
    auto appended=ring->open_reader(65536);assert(appended);
    std::string published(4096,'\0');
    assert(appended->read(published.data(),published.size())==4096);
    assert(published==staged);
    appended.reset();
    auto tail=ring->open_reader(69632);assert(tail);
    ring->finish();
    char out{};
    assert(tail->read(&out,1)==0);
    tail.reset();ring.reset();
    assert(!fs::exists(path));
    fs::remove_all(root);
}
