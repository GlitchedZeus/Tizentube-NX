#include "tizentube_nx/playback/verified_ring_hls_ingest.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
using ttnx::playback::RollingFileStream;
using ttnx::playback::VerifiedRingHlsIngest;
namespace fs=std::filesystem;
std::string ts(std::size_t packets=600) {
    std::string b(188*packets,static_cast<char>(0xff));
    for(std::size_t i=0;i<packets;++i)b[i*188]=0x47;
    const auto put=[&](std::size_t at,std::initializer_list<unsigned char> values){
        for(auto c:values)b[at++]=static_cast<char>(c);
    };
    put(0,{0x47,0x40,0x00,0x10,0,0x00,0xb0,0x0d,0,1,0xc1,
           0,0,0,1,0xe1,0,0,0,0,0});
    put(188,{0x47,0x41,0x00,0x10,0,0x02,0xb0,23,
             0,1,0xc1,0,0,0xe1,1,0xf0,0,0x1b,0xe1,1,0xf0,0});
    put(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return b;
}
struct SegmentReader {
    std::string bytes;
    std::size_t step=32768;
    bool bad_sync=false,fail_at_second_chunk=false,spoof_length=false;
    std::size_t admitted=0,emitted=0;
    struct Reply {bool ok{};std::size_t bytes{};};
    template<class Admit,class Emit>
    Reply stream(std::string_view url,Admit admit,Emit emit) {
        if(url!="https://r1.googlevideo.com/videoplayback")return {};
        ++admitted;
        if(!admit(bytes.size()+(spoof_length?188:0)))return {};
        for(std::size_t i=0;i<bytes.size();i+=step) {
            if(fail_at_second_chunk&&i>0)return {};
            auto part=std::string_view(bytes).substr(i,std::min(step,bytes.size()-i));
            ++emitted;
            if(!emit(part))return {};
        }
        return {true,bytes.size()};
    }
};
int main() {
    const auto dir=fs::temp_directory_path()/"ttnx_verified_ring_test";
    fs::remove_all(dir);fs::create_directory(dir);
    auto ring=RollingFileStream::create((dir/"stream.part").string(),188*1200);
    assert(ring);
    VerifiedRingHlsIngest bridge(ring);
    const auto good=ts(600);
    SegmentReader first;first.bytes=good;first.step=7;
    assert(bridge.ingest(first,"https://r1.googlevideo.com/videoplayback")==
           VerifiedRingHlsIngest::Result::Committed);
    assert(ring->stats().written_end==good.size());
    auto reader=ring->open_reader();assert(reader);
    std::string actual(good.size(),'\0');std::size_t off=0;
    while(off<actual.size()) {
        const auto n=reader->read(actual.data()+off,actual.size()-off);
        assert(n>0);off+=static_cast<std::size_t>(n);
    }
    assert(actual==good);
    SegmentReader denied;denied.bytes=good;denied.step=188;
    assert(bridge.ingest(denied,"https://r1.googlevideo.com/videoplayback")==
           VerifiedRingHlsIngest::Result::Committed);
    assert(ring->stats().written_end==2*good.size());
    assert(!ring->release_before(good.size()));
    SegmentReader full;full.bytes=good;
    assert(bridge.ingest(full,"https://r1.googlevideo.com/videoplayback")==
           VerifiedRingHlsIngest::Result::Backpressure);
    assert(full.emitted==0 && ring->stats().written_end==2*good.size());
    reader.reset();
    assert(ring->release_before(good.size()));
    assert(bridge.ingest(full,"https://r1.googlevideo.com/videoplayback")==
           VerifiedRingHlsIngest::Result::Committed);
    assert(ring->stats().written_end==3*good.size());
    assert(ring->stats().physical_size<=ring->capacity_bytes());
    auto view=ring->open_reader(2*good.size());
    assert(view);std::string current(good.size(),'\0');off=0;
    while(off<current.size()) {
        const auto n=view->read(current.data()+off,current.size()-off);
        assert(n>0);off+=static_cast<std::size_t>(n);
    }
    assert(current==good);
    view.reset();assert(ring->release_before(3*good.size()));
    SegmentReader failure;failure.bytes=good;failure.fail_at_second_chunk=true;
    const auto old_end=ring->stats().written_end;
    assert(bridge.ingest(failure,"https://r1.googlevideo.com/videoplayback")==
           VerifiedRingHlsIngest::Result::Transport);
    assert(ring->stats().written_end==old_end && !ring->staging());
    SegmentReader corrupt;corrupt.bytes=good;corrupt.bytes[188*3]=0;
    assert(bridge.ingest(corrupt,"https://r1.googlevideo.com/videoplayback")!=
           VerifiedRingHlsIngest::Result::Committed);
    assert(ring->stats().written_end==old_end && !ring->staging());
    SegmentReader wrong;wrong.bytes=good;wrong.spoof_length=true;
    assert(bridge.ingest(wrong,"https://r1.googlevideo.com/videoplayback")!=
           VerifiedRingHlsIngest::Result::Committed);
    assert(ring->stats().written_end==old_end && !ring->staging());
    SegmentReader bad_url;bad_url.bytes=good;
    assert(bridge.ingest(bad_url,"https://example.com/test")!=
           VerifiedRingHlsIngest::Result::Committed);
    assert(ring->stats().written_end==old_end);
    ring.reset();
    fs::remove_all(dir);
}
