#include "tizentube_nx/playback/local_live_hls_gate.hpp"
#include <cstdio>
#include <iostream>
#include <string>
using ttnx::playback::LocalLiveHlsGate;
using ttnx::playback::parse_local_live_hls;
int main(){
 int bad=0;
 auto check=[&](bool result,const char* test){if(!result){++bad;std::cerr<<"FAIL: "<<test<<'\n';}};
 const std::string valid="#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:3.000,\nv_00000.ts\n";
 LocalLiveHlsGate gate;
 check(parse_local_live_hls(valid,gate)&&gate.playable_prefix() && !gate.complete && gate.segments==1,"verified 3-second local prefix");
 const std::string sliding="#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:101\n#EXTINF:2.000,\nv_00101.ts\n#EXTINF:3.000,\nv_00102.ts\n#EXT-X-ENDLIST\n";
 check(parse_local_live_hls(sliding,gate)&&gate.complete&&gate.first_sequence==101&&gate.segments==2,"local rolling sequence and completed clip");
 auto reject=[&](std::string s,const char* test){check(!parse_local_live_hls(s,gate),test);};
 reject("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:3.000,\nhttps://evil.test/video.ts\n","remote URL rejected");
 reject(valid+"#EXT-X-KEY:METHOD=AES-128\n","encrypted/unknown HLS tag rejected");
 reject(valid+"#EXT-X-ENDLIST\n#EXT-X-KEY:URI=\"bad\"\n","trailing tag after endlist rejected");
 reject(valid.substr(0,valid.size()-1),"unterminated URI rejected");
 reject(valid+"#EXTINF:3.000,\nv_00002.ts\n","sequence discontinuity rejected");
 auto short_clip="#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:2.000,\nv_00000.ts\n";
 check(parse_local_live_hls(short_clip,gate) && !gate.playable_prefix(),"short prefix conservatively waits");
 reject(valid+"#EXTINF:NaN,\nv_00001.ts\n","invalid duration rejected");
 reject(valid+"#EXTINF:3.000,\nv_00001.ts\r\n","CRLF rejected");
 reject(valid+"#EXTINF:3.000,\n../v_00001.ts\n","path traversal rejected");
 reject(valid+"#EXTINF:3.000,\nv_00001.ts?expire=1\n","URL query rejected");
 reject(valid+"#EXTINF:3.000,\nv_00001.ts\n#EXT-X-STREAM-INF:BANDWIDTH=1\n","master playlist tag rejected");
 auto large=valid;for(int i=1;i<193;++i){char b[64];std::snprintf(b,sizeof(b),"#EXTINF:3.000,\nv_%05d.ts\n",i);large+=b;}
 reject(large,"metadata count limited to 192");
 check(!parse_local_live_hls(std::string(65537,'x'),gate),"manifest size capped");
 if(bad)std::cerr<<bad<<" local HLS gates failed\n";
 return bad?1:0;
}
