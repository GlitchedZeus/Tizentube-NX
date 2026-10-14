#include "tizentube_nx/playback/aac_mp4_structure.hpp"
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
using ttnx::playback::AacMp4Structure;
int main() {
    int failures=0;
    auto check=[&](bool good,const char* reason) {
        if(!good){++failures;std::cerr<<"FAIL "<<reason<<"\n";}
    };
    auto write32=[](std::string& s,std::size_t at,std::uint32_t n) {
        for(int i=3;i>=0;--i) {s[at+i]=static_cast<char>(n&255);n>>=8;}
    };
    std::string mp4(2048,'A');
    write32(mp4,0,24);mp4.replace(4,4,"ftyp");mp4.replace(8,4,"isom");
    write32(mp4,24,16);mp4.replace(28,4,"moov");
    write32(mp4,40,2008);mp4.replace(44,4,"mdat");

    AacMp4Structure gate;
    check(gate.begin(mp4.size()),"valid length initializes parser");
    for(std::size_t i=0;i<mp4.size();i+=7)
        check(gate.append(std::string_view(mp4).substr(i,7)),
              "split MP4 atom headers across arbitrary chunks");
    check(gate.complete(),"complete ftyp/moov/mdat accepted");

    gate.reset();check(gate.begin(mp4.size())&&
          gate.append(std::string_view(mp4).substr(0,120))&&!gate.complete(),
          "partial MP4 is never complete");
    auto broken=mp4;broken.replace(28,4,"free");
    gate.reset();check(gate.begin(broken.size())&&gate.append(broken)&&
                       !gate.complete(),"missing moov rejected");
    broken=mp4;write32(broken,40,9000);
    gate.reset();check(gate.begin(broken.size())&&!gate.append(broken),
                       "atom extending beyond declared file length rejected");
    broken=mp4;write32(broken,0,4);
    gate.reset();check(gate.begin(broken.size())&&!gate.append(broken),
                       "invalid atom header size rejected");
    broken=mp4;broken.replace(4,4,"moov");
    gate.reset();check(gate.begin(broken.size())&&!gate.append(broken),
                       "first MP4 box must be ftyp");
    gate.reset();check(!gate.begin(1023) &&
                       !gate.begin(128ULL*1024*1024+1),
                       "untrusted audio resource size bounded");

    if(failures)std::cerr<<failures<<" MP4 structure regressions\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
