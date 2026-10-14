#include "tizentube_nx/playback/ts_streams.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ttnx::playback {
MpegTsStreams inspect_mpeg_ts(std::string_view data) {
    MpegTsStreams out;
    constexpr std::size_t packet=188, limit=65536;
    const auto bytes=std::min(data.size(),limit);
    if(bytes<packet*2 ||
       static_cast<unsigned char>(data[0])!=0x47 ||
       static_cast<unsigned char>(data[packet])!=0x47)
        return out;
    out.recognized=true;
    int pmt_pid=-1;
    bool found_pmt=false;
    for(std::size_t offset=0;offset+packet<=bytes;offset+=packet) {
        const auto* p=reinterpret_cast<const unsigned char*>(data.data()+offset);
        if(p[0]!=0x47)return MpegTsStreams{};
        if(p[1]&0x80)continue;  // transport error indicator
        const int pid=((p[1]&0x1f)<<8)|p[2];
        const bool start=(p[1]&0x40)!=0;
        const unsigned afc=(p[3]>>4)&3;
        if(!start || afc==0 || afc==2)continue;
        std::size_t pos=4;
        if(afc==3) {
            const auto length=p[pos];
            pos+=1+length;
            if(pos>=packet)continue;
        }
        if(pos>=packet)continue;
        const auto pointer=p[pos];
        if(pointer>packet-pos-1)continue;
        pos+=1+pointer;
        if(pos+3>packet)continue;
        const auto* section=p+pos;
        const std::size_t table_len=((section[1]&0x0f)<<8)|section[2];
        // Reject multi-packet PSI until a section reassembler is reviewed;
        // never assume "not observed" means definitely absent.
        if(table_len<9 || table_len>1021 ||
           pos+3+table_len>packet)continue;
        const std::size_t end=3+table_len-4; // omit four-byte CRC
        if(pid==0 && section[0]==0 && pmt_pid<0) {
            if(end<12)continue;
            for(std::size_t j=8;j+4<=end;j+=4) {
                const auto program=(section[j]<<8)|section[j+1];
                if(program) {
                    pmt_pid=((section[j+2]&0x1f)<<8)|section[j+3];
                    break;
                }
            }
        } else if(pid==pmt_pid && section[0]==2) {
            if(end<12)continue;
            const auto info_len=((section[10]&0x0f)<<8)|section[11];
            std::size_t j=12+info_len;
            if(j>end)continue;
            while(j+5<=end) {
                const auto type=section[j];
                const auto es_len=((section[j+3]&0x0f)<<8)|section[j+4];
                if(j+5+es_len>end)break;
                if(type==0x1b)out.h264=true;
                if(type==0x0f || type==0x11)out.aac=true;
                j+=5+es_len;
            }
            found_pmt=true;
            break;
        }
    }
    if(!found_pmt) {
        // Signature itself was TS; no verified stream metadata yet.
        out.h264=false;out.aac=false;
    }
    return out;
}
} // namespace ttnx::playback
