#include "tizentube_nx/playback/direct_av_plan.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace ttnx;
static constexpr const char* kV=
 "https://r1---sn-test.googlevideo.com/videoplayback?foo=123";
static constexpr const char* kA=
 "https://r2---sn-test.googlevideo.com/videoplayback?foo=456";
int main(){
    core::ContentItem item{core::ContentKind::Video,"IzJ7R4EnYmI",
                           "Verified Search video",false,false};
    playback::ExtractionResult r;
    r.video_id=item.id;r.duration_ms=30*60*1000;
    r.video.push_back({kV,playback::VideoCodec::H264,
        playback::AudioCodec::Unknown,1280,720,30,8,2000000,false,false});
    r.audio.push_back({kA,playback::AudioCodec::Aac,128000,false});
    auto allowed=playback::admit_direct_av_plan(r,item);
    assert(allowed && allowed->duration_ms==r.duration_ms);
    assert(allowed->height==720 && allowed->fps==30);
    assert(allowed->video_url==kV && allowed->audio_url==kA);
    item.kind=core::ContentKind::Short;
    assert(!playback::admit_direct_av_plan(r,item));
    item.kind=core::ContentKind::Video;
    item.promoted=true;assert(!playback::admit_direct_av_plan(r,item));
    item.promoted=false;item.shopping=true;
    assert(!playback::admit_direct_av_plan(r,item));
    item.shopping=false;item.id="SomethingEl";
    assert(!playback::admit_direct_av_plan(r,item));
    item.id=r.video_id;
    r.is_short=true;assert(!playback::admit_direct_av_plan(r,item));
    r.is_short=false;r.is_live=true;
    assert(!playback::admit_direct_av_plan(r,item));
    r.is_live=false;
    r.duration_ms=13ULL*60*60*1000;
    assert(!playback::admit_direct_av_plan(r,item));
    r.duration_ms=1800000;
    r.audio.front().url="https://accounts.nintendo.com/videoplayback";
    assert(!playback::admit_direct_av_plan(r,item));
    r.audio.front().url=kA;
    r.video.front().height=1080;assert(!playback::admit_direct_av_plan(r,item));
    r.video.front().height=720;
    r.video.front().fps=120;assert(!playback::admit_direct_av_plan(r,item));
    r.video.front().fps=30;
    r.video.front().drm=true;assert(!playback::admit_direct_av_plan(r,item));
    r.video.front().drm=false;
    r.audio.front().codec=playback::AudioCodec::Opus;
    assert(!playback::admit_direct_av_plan(r,item));
    r.audio.front().codec=playback::AudioCodec::Aac;
    r.audio.clear();r.video.front().muxed_audio=playback::AudioCodec::Aac;
    assert(!playback::admit_direct_av_plan(r,item)); // not a separate pair
}
