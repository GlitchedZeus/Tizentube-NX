#include "tizentube_nx/playback/rolling_stream_coordinator.hpp"
#include <cstdint>
#include <iostream>
using namespace ttnx::playback;
using R=RollingStreamCoordinator;
using Track=R::Track;
using Admission=R::Admission;
namespace {int errors=0;void ck(bool ok,const char* msg){if(!ok){++errors;std::cerr<<"FAIL "<<msg<<'\n';}}}
int main(){
    constexpr std::uint64_t MiB=1024ULL*1024;
    R r;
    ck(!r.begin(0,1080,30),"forbid >720p");
    ck(!r.begin(0,720,120),"forbid unsupported native 120fps");
    ck(r.begin(0,720,60) && r.selected_height()==720 && r.native_fps()==60,
       "native source fps is retained (never artificially converted)");
    r.mark_codecs_verified(true,true);
    ck(!r.startup_ready(),"codec metadata alone never unlocks startup");
    ck(r.reserve(Track::Video,0,0,3000,2*MiB)==Admission::Accepted,
       "bounded video transfer reserved");
    auto video=*r.pending_ticket();
    ck(r.reserved_bytes()==2*MiB,"count in-flight bytes against app quota");
    ck(r.reserve(Track::Audio,0,0,3000,MiB)==Admission::Busy,
       "no double-issued single-slot fetch");
    ck(!r.committed({video.request_id+100,video.track,video.index,0,3000,2*MiB},
                    MiB,true,true) && r.pending_ticket().has_value(),
       "late ticket cannot mutate active fetch");
    auto tampered=video;
    tampered.end_ms=30000;
    ck(!r.committed(tampered,MiB,true,true) && r.pending_ticket().has_value(),
       "tampered reservation ticket cannot corrupt retained timeline");
    ck(r.committed(video,MiB,true,true) && !r.startup_ready(),
       "verified durable H264 alone cannot start silent playback");
    ck(r.reserve(Track::Audio,0,0,3000,MiB)==Admission::Accepted,
       "bounded AAC transfer reserved");
    auto audio=*r.pending_ticket();
    ck(r.committed(audio,MiB/2,true,true) && r.startup_ready(),
       "three seconds of both tracks allow early playback");
    ck(r.pin(Track::Audio,0,true),"active decoder pins audio segment");
    ck(r.advance_playhead(6000) && r.evictable_bytes()==MiB,
       "finished video is eligible but pinned audio is not");
    ck(!r.acknowledge_unlinked(Track::Audio,0),"decoder-pinned audio cannot release quota");
    ck(!r.seek(120000,true),"seek must join decoder and unpin before clearing media");
    ck(r.pin(Track::Audio,0,false) && r.acknowledge_unlinked(Track::Audio,0),
       "closed decoder allows accounting after physical unlink");
    ck(!r.seek(120000,false),"seek requires proof prior files were cleared");
    ck(r.seek(120000,true) && !r.startup_ready() && r.media_bytes()==0,
       "seek resets to an arbitrary timestamp and clears prior timeline");
    r.mark_codecs_verified(true,true);
    ck(r.reserve(Track::Video,0,120000,120100,MiB)==Admission::Accepted,
       "short seek prefix admitted");
    video=*r.pending_ticket();
    ck(r.committed(video,MiB,true,true) && !r.startup_ready(),
       "seeked timeline cannot mistake absolute position for buffered seconds");
    ck(r.seek(120000,true),"restart seek after insufficient prefix was purged");
    r.mark_codecs_verified(true,true);
    ck(r.reserve(Track::Video,0,120000,123000,MiB)==Admission::Accepted,
       "video at seek anchor admitted");
    video=*r.pending_ticket();
    ck(r.committed(video,MiB,true,true),"seek video persisted");
    ck(r.reserve(Track::Audio,0,120000,123000,MiB/2)==Admission::Accepted,
       "AAC at seek anchor admitted");
    audio=*r.pending_ticket();
    ck(r.committed(audio,MiB/2,true,true) && r.startup_ready(),
       "seeked source starts from its own 3-second verified window");
    ck(!r.can_issue_network_request(190000) &&
       r.can_issue_network_request(123000),
       "pre-DNS gate blocks ahead-of-playback media without a CDN request");
    ck(r.reserve(Track::Video,1,190000,193000,MiB)==Admission::Invalid,
       "cannot prefetch unlimited future video before playback advances");
    ck(r.reserve(Track::Video,1,123000,126000,23*MiB)==Admission::Invalid,
       "one unbounded 23-MiB chunk not fetched");
    ck(r.reserve(Track::Video,1,123000,126000,16*MiB)==Admission::Accepted,
       "one bounded in-flight chunk admitted");
    auto ticket=*r.pending_ticket();
    ck(!r.committed(ticket,16*MiB+1,true,true) &&
       r.state()==R::State::Failed,
       "larger-than-reserved chunk fails closed without inflating quota");
    ck(r.seek(180000,true),"failed session may reset after verified cleanup");
    ck(r.reserve(Track::Video,0,180000,183000,16*MiB)==Admission::Accepted,
       "large next stream starts");
    ticket=*r.pending_ticket();
    r.cancel_pending();
    ck(!r.committed(ticket,MiB,true,true) && r.media_bytes()==0,
       "cancelled worker callback cannot publish stale generation");
    ck(r.cancel(true) && r.state()==R::State::Cancelled,
       "cancel after worker join and purge completes");
    ck(r.begin(0,720,30),"reusable after cancelled transfer");
    constexpr auto per_video=1536*1024ULL, per_aac=128*1024ULL;
    for(std::uint32_t i=0;i<1200;++i){
        const auto start=3000ULL*i;
        ck(r.advance_playhead(start),"playhead advances monotonically");
        if(i>2){
            ck(r.acknowledge_unlinked(Track::Video,i-3),"video retire after decoder close");
            ck(r.acknowledge_unlinked(Track::Audio,i-3),"audio retire after decoder close");
        }
        ck(r.reserve(Track::Video,i,start,start+3000,per_video)==Admission::Accepted,
           "720p rolling request admitted");
        auto v=*r.pending_ticket();
        ck(r.committed(v,per_video,true,true),"video committed");
        ck(r.reserve(Track::Audio,i,start,start+3000,per_aac)==Admission::Accepted,
           "AAC rolling request admitted");
        auto a=*r.pending_ticket();
        ck(r.committed(a,per_aac,true,true),"audio committed");
        ck(r.reserved_bytes()<=RollingMediaBudget::kCapacityBytes,
           "total active and pending storage below 24 MiB");
    }
    ck(r.peak_media_bytes()<12*MiB && r.media_bytes()<12*MiB,
       "one-hour video's temporary media remains a few MiB, not many GiB");
    ck(r.cancel(true) && r.media_bytes()==0,
       "stop after verified purge drops all accounting");
    ck(r.begin(0,720,30),"tiny-segment accounting ledger starts");
    for(std::uint32_t i=0;i<RollingMediaBudget::kMaxPieces;++i) {
        const auto begin=1000ULL*i;
        ck(r.advance_playhead(begin),"tiny-segment playhead advances");
        ck(r.reserve(Track::Muxed,i,begin,begin+1000,128)==Admission::Accepted,
           "small segment admitted before the metadata ceiling");
        auto ticket=*r.pending_ticket();
        ck(r.committed(ticket,128,true,true),"tiny segment accounted");
    }
    ck(r.advance_playhead(192000),"playhead reaches the ledger ceiling");
    ck(!r.can_issue_network_request(192000),
       "full metadata ledger rejects CDN request before TLS handshake");
    ck(r.reserve(Track::Muxed,192,192000,193000,128)==Admission::Backpressure &&
       !r.pending_ticket().has_value() && r.reserved_bytes()==192*128 &&
       r.state()==R::State::Backpressure,
       "192-piece metadata ceiling blocks I/O before transfer, not after a write");
    ck(r.acknowledge_unlinked(Track::Muxed,0),
       "decoder-confirmed old file release frees one metadata slot");
    ck(r.reserve(Track::Muxed,192,192000,193000,128)==Admission::Accepted,
       "small-segment network request resumes when ledger space becomes available");
    auto resumed=*r.pending_ticket();
    ck(r.committed(resumed,128,true,true),"recovered metadata ledger commits");
    ck(r.cancel(true),"metadata backpressure teardown clears the session");
    return errors?1:0;
}
