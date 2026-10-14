#include "tizentube_nx/playback/verified_ring_hls_ingest.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
namespace fs=std::filesystem;
using ttnx::playback::RollingFileStream;
using ttnx::playback::VerifiedRingHlsIngest;
struct ApprovedLocalFixture {
    std::string file;
    struct Reply {bool ok{};std::size_t bytes{};};
    template<class Admit,class Emit>
    Reply stream(std::string_view url,Admit admit,Emit emit) {
        if(url!="https://r1.googlevideo.com/videoplayback")return {};
        const auto count=fs::file_size(file);
        if(!admit(count))return {};
        std::ifstream input(file,std::ios::binary);
        if(!input)return {};
        std::size_t received=0;
        char buffer[32768];
        while(input) {
            input.read(buffer,sizeof(buffer));
            const auto amount=input.gcount();
            if(amount>0){
                if(!emit(std::string_view(buffer,static_cast<std::size_t>(amount))))return {};
                received+=static_cast<std::size_t>(amount);
            }
        }
        return {input.eof(),received};
    }
};
int main(int argc,char** argv) {
    assert(argc==2);
    const std::string input=argv[1];
    const auto dir=fs::temp_directory_path()/"ttnx_real_av_ring_fixture";
    fs::remove_all(dir);fs::create_directory(dir);
    const auto path=(dir/"media.part").string();
    auto ring=RollingFileStream::create(path,1024*1024);
    assert(ring);
    ApprovedLocalFixture network{input};
    {
        VerifiedRingHlsIngest session(ring);
        assert(session.ingest(network,"https://r1.googlevideo.com/videoplayback")==
               VerifiedRingHlsIngest::Result::Committed);
    }
    const auto size=fs::file_size(input);
    assert(ring->stats().written_end==size);
    assert(ring->stats().physical_size<=1024*1024);
    std::ifstream original(input,std::ios::binary);
    auto reader=ring->open_reader();assert(reader);
    std::string block(32768,'\0');
    char expected[32768];
    std::uint64_t read_bytes=0;
    while(read_bytes<size) {
        const auto count=reader->read(block.data(),block.size());
        assert(count>0);
        original.read(expected,count);
        assert(original.gcount()==count);
        assert(std::string_view(block.data(),static_cast<std::size_t>(count))==
               std::string_view(expected,static_cast<std::size_t>(count)));
        read_bytes+=static_cast<std::uint64_t>(count);
    }
    assert(read_bytes==size);
    reader.reset();
    ring.reset();
    assert(!fs::exists(path)); // last owner deletes all cached media
    fs::remove_all(dir);
}
