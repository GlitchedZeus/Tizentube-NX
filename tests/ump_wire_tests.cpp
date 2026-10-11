#include "tizentube_nx/playback/ump_wire.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ttnx::playback;
namespace {
int fail_count=0;
void ck(bool ok,const char* msg) {
    if(!ok){++fail_count;std::cerr<<"FAIL "<<msg<<"\n";}
}
void parse_tests() {
    // UMP MEDIA envelope: part type 21, payload size 5,
    // media discriminator byte 0 + four MP4 bytes.
    const std::vector<uint8_t> media={21,5,0,'f','t','y','p'};
    const auto parsed=parse_bounded_ump_media(media.data(),media.size());
    ck(parsed && std::string(parsed->begin(),parsed->end())=="ftyp",
       "MEDIA payload stripped of the one-byte discriminator");
    const std::vector<uint8_t> malformed={21,10,0,'f','t','y','p'};
    ck(!parse_bounded_ump_media(malformed.data(),malformed.size()),
       "truncated UMP part rejected");
    ck(!parse_bounded_ump_media(nullptr,4),"null pointer rejected");
    const std::vector<uint8_t> redirect={21,5,0,'f','t','y','p',43,4,10,2,'x','y'};
    ck(!parse_bounded_ump_media(redirect.data(),redirect.size()),
       "UMP redirect not auto-followed or treated as media");
    const std::vector<uint8_t> protected_data={21,5,0,'f','t','y','p',58,2,8,1};
    ck(!parse_bounded_ump_media(protected_data.data(),protected_data.size()),
       "UMP media with server protection marker is refused");
    const std::vector<uint8_t> only_redirect={43,4,10,2,'x','y'};
    ck(!parse_bounded_ump_media(only_redirect.data(),only_redirect.size()),
       "redirect-only response is not a video");
    std::vector<uint8_t> oversized(1024*1024+128*1024+1,'X');
    ck(!parse_bounded_ump_media(oversized.data(),oversized.size()),
       "oversized response refused before parser allocation");
}
void url_tests() {
    const std::string good=
        "https://r1---sn-test.googlevideo.com/videoplayback?expire=123&sig=signed";
    auto url=reviewed_ump_chunk_url(good,0,2*1024*1024,1);
    ck(url.has_value(),"reviewed UMP range can be built");
    if(url) {
        ck(url->find("range=0-1048575")!=std::string::npos,
           "exact first 1 MiB range");
        ck(url->find("ump=1")!=std::string::npos &&
           url->find("srfvp=1")!=std::string::npos &&
           url->find("alr=yes")!=std::string::npos &&
           url->find("rn=1")!=std::string::npos,"required UMP arguments");
    }
    url=reviewed_ump_chunk_url(good,2*1024*1024-15,2*1024*1024,2);
    ck(url && url->find("range=2097137-2097151")!=std::string::npos,
       "final UMP range ends at exact resource boundary");
    ck(!reviewed_ump_chunk_url(
        "https://accounts.nintendo.com/videoplayback",0,100,1),
       "Nintendo host blocked before transport");
    ck(!reviewed_ump_chunk_url(
        "https://r1.googlevideo.com.attacker.test/videoplayback",0,100,1),
       "suffix-confused host blocked");
    ck(!reviewed_ump_chunk_url(good,100,100,1),"EOF not requested");
    ck(!reviewed_ump_chunk_url(good,0,0,1),"unknown length denied");
    ck(!reviewed_ump_chunk_url(good,0,513ULL*1024*1024,1),
       "large resource rejected");
    ck(!reviewed_ump_chunk_url(good,0,100,-1),"invalid UMP sequence rejected");
    ck(!reviewed_ump_chunk_url(good,0,100,4097),"sequence flood rejected");
}
}
int main() {
    parse_tests();url_tests();
    if(fail_count)std::cerr<<fail_count<<" unsafe UMP paths\n";
    return fail_count?EXIT_FAILURE:EXIT_SUCCESS;
}
