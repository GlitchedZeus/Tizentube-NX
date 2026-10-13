#include "tizentube_nx/playback/streaming_ts_validator.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <string>
using ttnx::playback::StreamingTsValidator;
std::string ts(bool aac=true,std::size_t packets=2) {
    std::string b(188*packets,static_cast<char>(0xff));
    for(std::size_t i=0;i<packets;++i)b[i*188]=0x47;
    const auto put=[&](std::size_t at,std::initializer_list<unsigned char> bytes){
        for(auto byte:bytes)b[at++]=static_cast<char>(byte);
    };
    put(0,{0x47,0x40,0x00,0x10,0,0x00,0xb0,0x0d,0,1,0xc1,
           0,0,0,1,0xe1,0,0,0,0,0});
    put(188,{0x47,0x41,0x00,0x10,0,0x02,0xb0,
             static_cast<unsigned char>(aac?23:18),
             0,1,0xc1,0,0,0xe1,1,0xf0,0,0x1b,0xe1,1,0xf0,0});
    if(aac)put(188+5+17,{0x0f,0xe1,2,0xf0,0});
    return b;
}
int main(){
    const auto good=ts(true,5000);
    for(const std::size_t step:{1u,7u,187u,188u,191u,32000u}) {
        StreamingTsValidator v;
        for(std::size_t i=0;i<good.size();i+=step)
            assert(v.push(std::string_view(good).substr(i,std::min(step,good.size()-i))));
        assert(v.buffered_bytes()<=65536&&v.bytes()==good.size());
        assert(v.finish()&&v.valid());
        assert(!v.push(good));
        assert(!v.finish());
    }
    {StreamingTsValidator v;assert(!v.push({}));assert(!v.valid());}
    {StreamingTsValidator v;const auto invalid=ts(false,4);
     assert(v.push(invalid));assert(!v.finish());}
    {StreamingTsValidator v;auto invalid=ts(true,5);invalid[3*188]=0;
     assert(!v.push(invalid));assert(!v.finish());}
    {StreamingTsValidator v;auto invalid=ts(true,5);invalid.pop_back();
     assert(v.push(invalid));assert(!v.finish());}
    {StreamingTsValidator v(376);auto valid=ts(true,3);
     assert(!v.push(valid));assert(v.bytes()==0);}
    {StreamingTsValidator v;auto valid=ts(true,3);
     assert(v.push(valid));assert(v.finish());assert(v.buffered_bytes()<=65536);}
    return 0;
}
