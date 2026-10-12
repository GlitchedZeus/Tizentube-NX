#include "tizentube_nx/playback/hls_event_playlist.hpp"
#include <cstdlib>
#include <iostream>
using namespace ttnx::playback;
int main() {
    int fails=0;
    auto ck=[&](bool ok,const char* name) {
        if(!ok){++fails;std::cerr<<"FAIL "<<name<<"\n";}
    };
    HlsMediaPlaylist plan;
    plan.segments={
        {"https://r1.googlevideo.com/videoplayback?expire=SECRET&sq=1",6000},
        {"https://r1.googlevideo.com/videoplayback?expire=SECRET&sq=2",6000},
        {"https://r1.googlevideo.com/videoplayback?expire=SECRET&sq=3",6000},
    };
    plan.total_duration_ms=18000;
    auto prefix=make_hls_event_snapshot(plan,2,false);
    ck(prefix && prefix->find("v_00000.ts")!=std::string::npos &&
       prefix->find("v_00001.ts")!=std::string::npos &&
       prefix->find("v_00002.ts")==std::string::npos &&
       prefix->find("PLAYLIST-TYPE:EVENT")!=std::string::npos &&
       prefix->find("ENDLIST")==std::string::npos,
       "two segment event prefix remains open for append");
    ck(prefix && prefix->find("googlevideo")==std::string::npos &&
       prefix->find("SECRET")==std::string::npos &&
       prefix->find("https:")==std::string::npos,
       "signed bearer source never appears in local playlist");
    auto grown=make_hls_event_snapshot(plan,3,false);
    ck(grown && prefix && grown->starts_with(*prefix) &&
       grown->find("v_00002.ts")!=std::string::npos,
       "event snapshot is append-only with stable media sequence");
    auto finished=make_hls_event_snapshot(plan,3,true);
    ck(finished && grown &&
       *finished==(*grown+"#EXT-X-ENDLIST\n"),
       "ENDLIST only finalizes complete cached media");
    ck(!make_hls_event_snapshot(plan,2,true) &&
       !make_hls_event_snapshot(plan,0,false) &&
       !make_hls_event_snapshot(plan,4,false),
       "missing, overrun or prematurely finalized segment sets refused");
    plan.init_segment_url="https://r1.googlevideo.com/videoplayback?init=SECRET";
    ck(!make_hls_event_snapshot(plan,1,false,true),
       "init segment and MPEG-TS extension cannot be mismatched");
    auto fragment=make_hls_event_snapshot(plan,1,false,false);
    ck(fragment && fragment->find("v_init.m4s")!=std::string::npos &&
       fragment->find("v_00000.m4s")!=std::string::npos,
       "fMP4 events refer to their local init chunk");
    plan.segments[1].duration_ms=0;
    ck(!make_hls_event_snapshot(plan,1,false,false),
       "invalid later durations cannot be hidden by a short prefix");
    if(fails)std::cerr<<fails<<" HLS event snapshot failures\n";
    return fails?EXIT_FAILURE:EXIT_SUCCESS;
}
