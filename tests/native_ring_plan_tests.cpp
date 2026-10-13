#include "tizentube_nx/playback/native_ring_plan.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <string>
using namespace ttnx;
using playback::admit_native_ring_plan;
namespace {
constexpr const char* id="2kD9v7Q3vec";
constexpr const char* media="https://r1.googlevideo.com/videoplayback?sig=validated";
playback::HlsProbePlan good() {
    playback::HlsProbePlan p;
    p.source.video_id=id;
    p.source.duration_ms=6000;
    p.selected.width=1280;p.selected.height=720;p.selected.fps=30;
    p.video_segments.segments={{media,3000},{media,3000}};
    p.video_segments.total_duration_ms=6000;
    return p;
}
core::ContentItem normal() {
    return {core::ContentKind::Video,id,"Normal, filtered Search result",false,false};
}
}
int main() {
    {
        auto accepted=admit_native_ring_plan(good(),normal());
        assert(accepted && accepted->urls.size()==2);
        assert(accepted->durations_ms.size()==2);
        assert(accepted->total_duration_ms==6000);
    }
    {
        auto p=good();p.source.duration_ms=3000; // allowed +3s source drift
        assert(admit_native_ring_plan(p,normal()));
        p.source.duration_ms=2999;
        assert(!admit_native_ring_plan(p,normal()));
    }
    {
        auto p=good();
        auto item=normal();
        item.kind=core::ContentKind::Short;
        assert(!admit_native_ring_plan(p,item));
        item.kind=core::ContentKind::Channel;
        assert(!admit_native_ring_plan(p,item));
        item=normal();item.promoted=true;
        assert(!admit_native_ring_plan(p,item));
        item=normal();item.shopping=true;
        assert(!admit_native_ring_plan(p,item));
        item=normal();item.id="abcdefghijk";
        assert(!admit_native_ring_plan(p,item));
        p.source.video_id="bad ID";
        assert(!admit_native_ring_plan(p,normal()));
    }
    {
        auto p=good();
        p.selected.height=1080;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.selected.width=0;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.selected.fps=61;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.selected.height=0;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.selected.direct_media_playlist=true;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.selected.audio_playlist_url="https://r1.googlevideo.com/videoplayback";
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.source.external_aac_url="https://r1.googlevideo.com/videoplayback";
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.audio_segments.emplace();
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.init_segment_url=media;
        assert(!admit_native_ring_plan(p,normal()));
    }
    {
        auto p=good();
        p.video_segments.segments[0].duration_ms=0;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.segments[0].duration_ms=60001;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.total_duration_ms=5999;
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.segments.clear();
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.segments[0].url="https://unapproved.example/videoplayback";
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.segments[0].url="http://r1.googlevideo.com/videoplayback";
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.segments[0].url="https://r1.googlevideo.com/videoplayback#fragment";
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.video_segments.segments[0].url=std::string(4097,'a');
        assert(!admit_native_ring_plan(p,normal()));
    }
    {
        // 4096 segments and 12h are accepted only if source timing agrees.
        auto p=good();
        p.source.duration_ms=4096ULL*3000;
        p.video_segments.segments.assign(4096,{media,3000});
        p.video_segments.total_duration_ms=p.source.duration_ms;
        const auto accepted=admit_native_ring_plan(p,normal());
        assert(accepted && accepted->urls.size()==4096);
        p.video_segments.segments.push_back({media,3000});
        assert(!admit_native_ring_plan(p,normal()));
        p=good();p.source.duration_ms=12ULL*60*60*1000+1;
        assert(!admit_native_ring_plan(p,normal()));
    }
}
