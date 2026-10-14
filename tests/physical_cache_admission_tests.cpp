#include "tizentube_nx/playback/physical_cache_admission.hpp"
#include "tizentube_nx/net/http1_media_stream.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
using ttnx::playback::PhysicalCacheAdmission;
int main() {
    using C=PhysicalCacheAdmission;
    constexpr std::uint64_t size=188*6000;
    std::uint64_t used=0;
    for(unsigned i=0;i<14;++i) {
        assert(C::decide(used,size)==C::Result::Accepted);
        used+=size;
    }
    assert(used>8*1024*1024 && used<C::kMediaCapacity);
    assert(C::decide(used,16*1024*1024)==C::Result::Backpressure);
    assert(C::decide(used,0)==C::Result::Invalid);
    assert(C::decide(used,16*1024*1024+1)==C::Result::Invalid);
    assert(C::decide(C::kMediaCapacity,376)==C::Result::Backpressure);
    assert(C::decide(C::kMediaCapacity+1,376)==C::Result::Invalid);
    // On a precisely full cache, even a valid small HTTP body is declined
    // at header time: no body is ever emitted or written to SD.
    const std::string body(188*2,'X');
    const std::string response="HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nContent-Length: "+
      std::to_string(body.size())+"\r\n\r\n"+body;
    unsigned published=0,checks=0;
    ttnx::net::Http1MediaStream parser([&](std::size_t length) {
        ++checks;
        return C::decide(C::kMediaCapacity,length)==C::Result::Accepted;
    });
    assert(!parser.feed(response,[&](std::string_view){++published;return true;}));
    assert(checks==1 && published==0);
    return 0;
}
