#include "tizentube_nx/playback/ts_streams.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace ttnx::playback;
namespace {
int errors=0;
void ck(bool v,const char* why){if(!v){++errors;std::cerr<<"FAIL "<<why<<"\n";}}
std::string fixture(bool audio) {
    std::string bytes(188*2,static_cast<char>(0xff));
    auto put=[&](std::size_t off,std::initializer_list<unsigned char> input){
        for(auto ch:input)bytes[off++]=static_cast<char>(ch);
    };
    // PAT declares PMT PID 0x100, synthetic CRC placeholder. The parser
    // checks framing only; decoder never treats fixture media as real video.
    put(0,{0x47,0x40,0x00,0x10,0,
           0x00,0xb0,0x0d,0,1,0xc1,0,0,0,1,0xe1,0,0,0,0,0});
    const int n=audio?23:18;
    put(188,{0x47,0x41,0x00,0x10,0,
             0x02,0xb0,static_cast<unsigned char>(n),0,1,0xc1,0,0,
             0xe1,1,0xf0,0,
             0x1b,0xe1,1,0xf0,0});
    if(audio)put(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return bytes;
}
}
int main(){
    const auto full=inspect_mpeg_ts(fixture(true));
    ck(full.recognized && full.h264 && full.aac,
       "PAT/PMT reveals H264 and AAC tracks");
    const auto silent=inspect_mpeg_ts(fixture(false));
    ck(silent.recognized && silent.h264 && !silent.aac,
       "H264-only segment is NOT accepted as containing AAC");
    auto invalid=fixture(true);invalid[188]='x';
    ck(!inspect_mpeg_ts(invalid).recognized,
       "broken TS packet sync fails closed");
    ck(!inspect_mpeg_ts("an HTML error response").recognized,
       "HTTP 200 HTML is not media");
    if(errors)std::cerr<<errors<<" TS probe regressions\n";
    return errors?EXIT_FAILURE:EXIT_SUCCESS;
}
