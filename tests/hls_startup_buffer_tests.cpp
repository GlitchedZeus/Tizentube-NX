#include "tizentube_nx/playback/hls_startup_buffer.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
int main() {
    using ttnx::playback::HlsStartupBuffer;
    int failed=0;
    auto ck=[&](bool valid,const char* reason) {
        if(!valid){++failed;std::cerr<<"FAIL "<<reason<<"\n";}
    };
    HlsStartupBuffer b;
    ck(b.configure({6000,6000,6000,6000}) &&
       b.required_segments()==2 && !b.prepared(),
       "first two 6s segments are the bounded target");
    b.mark_video_codec(true);
    ck(b.add_video(0,6000,1024) && !b.prepared(),
       "one video segment does not grant ready state");
    b.mark_complete_audio(true);
    ck(!b.prepared() && b.add_video(1,6000,1024) &&
       b.prepared() && b.prefix_ms()==12000,
       "complete audio and two verified video segments satisfy readiness");
    ck(b.add_video(2,6000,1024) && b.prefix_ms()==12000,
       "later media cannot inflate initial startup buffer");
    b.reset();ck(!b.prepared(),"cancel resets the buffer gate");
    ck(b.configure({3000,3000,3000,3000,3000}) &&
       b.required_segments()==4,
       "four smaller segments needed for twelve seconds");
    ck(!b.add_video(2,3000,1024),"out-of-order video cannot skip data");
    for(std::size_t i=0;i<4;++i)
        ck(b.add_video(i,3000,1024),"ordered segments accepted");
    b.mark_complete_audio(true);
    ck(!b.prepared(),"unverified codec still blocks readiness");
    b.mark_video_codec(true);
    ck(b.prepared(),"codec gate is required");
    b.reset();
    ck(b.configure(std::vector<std::uint64_t>(12,2000)) &&
       b.required_segments()==6,
       "initial prefetch limited to six segments");
    b.reset();
    ck(!b.configure({0,6000}) && !b.configure({60001}) &&
       !b.configure({}) &&
       !b.configure(std::vector<std::uint64_t>(4097,1000)),
       "invalid and oversized playlist rejected");
    ck(b.configure({6000,6000}) &&
       !b.add_video(0,6000,16ULL*1024*1024+1),
       "oversized segment rejected");
    if(failed)std::cerr<<failed<<" startup buffer failures\n";
    return failed?EXIT_FAILURE:EXIT_SUCCESS;
}
