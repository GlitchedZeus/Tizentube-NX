#include "tizentube_nx/playback/rolling_media_budget.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
using namespace ttnx::playback;
namespace {
int errors = 0;
void check(bool value, const char* name) {
    if (!value) { ++errors; std::cerr << "FAIL: " << name << '\n'; }
}
}
int main() {
    using B = StorageBudget;
    using W = RollingMediaBudget;
    using T = W::Track;
    using R = W::AddResult;
    constexpr auto MiB = 1024ULL * 1024;
    static_assert(B::kTotalAppBytes == 100'000'000);
    static_assert(B::kMaxNroAndAssetsBytes + B::kRollingMediaBytes +
                  B::kReservedPersistentBytes == B::kTotalAppBytes);
    check(B::fits(B::kMaxNroAndAssetsBytes,B::kReservedPersistentBytes,
                  B::kRollingMediaBytes), "exact 100 MB budget fits");
    check(!B::fits(B::kMaxNroAndAssetsBytes + 1, 0, 0), "oversize NRO rejected");
    check(!B::fits(0, B::kReservedPersistentBytes + 1, 0), "oversize metadata rejected");
    check(!B::fits(0, 0, B::kRollingMediaBytes + 1), "oversize media rejected");
    W w;
    check(!w.startup_ready(),"empty stream never ready");
    w.set_codec_verification(true,true);
    check(!w.startup_ready(),"verified tracks without frames not ready");
    check(w.add(T::Video,0,0,1500,MiB)==R::Accepted, "first video accepted");
    check(w.add(T::Video,1,1500,3000,MiB)==R::Accepted, "second video accepted");
    check(w.add(T::Audio,0,0,3000,MiB/4)==R::Accepted, "first AAC accepted");
    check(w.startup_ready(),"3 seconds of verified synchronized audio/video unlock startup");
    check(w.add(T::Video,1,3000,4000,MiB)==R::OutOfOrder,"duplicate segment rejected");
    check(w.add(T::Video,3,2999,4000,MiB)==R::OutOfOrder,"overlap rejected");
    check(w.add(T::Video,2,3000,4000,0)==R::Invalid,"zero bytes rejected");
    check(w.add(T::Video,2,3000,4000,std::numeric_limits<std::uint64_t>::max())==R::Invalid,
          "oversized overflow safely rejected");
    check(w.pin(T::Audio,0,true),"pin audio while decoder owns it");
    check(w.evictable_bytes(5000)==2*MiB,"only unpinned played video evictable");
    check(!w.commit_eviction(T::Audio,0,5000),"never unlink pinned audio");
    check(!w.clear_epoch(),"cannot clear active epoch with pins");
    check(w.commit_eviction(T::Video,0,5000) && w.used_bytes()==MiB+MiB/4,
          "committed unlink releases quota");
    check(w.pin(T::Audio,0,false),"decoder releases audio");
    check(w.commit_eviction(T::Audio,0,5000),"unpin permits audio eviction");
    check(w.clear_epoch() && w.used_bytes()==0 && !w.startup_ready(),
          "seek/cancel resets quota and verified state");
    check(w.add(T::Muxed,0,0,3500,MiB)==R::Accepted,
          "verified muxed media uses one accounting reservation");
    w.set_codec_verification(true,true);
    check(w.startup_ready(),"muxed codec-verified stream supplies both tracks");
    check(w.clear_epoch(),"reset muxed epoch");
    check(w.add(T::Video,0,0,4000,23*MiB)==R::Accepted,"first large chunk");
    check(w.add(T::Audio,0,0,4000,2*MiB)==R::AtCapacity,
          "all A/V share one hard bounded reservoir");
    check(w.high_water_bytes()<=B::kRollingMediaBytes,"never exceed disk reservation");
    check(w.clear_epoch(),"clear after failed admission");
    check(w.add(T::Video,0,100,4100,MiB)==R::Accepted,"first future-only video");
    check(w.add(T::Audio,0,0,3500,MiB)==R::Accepted,"audio from zero");
    w.set_codec_verification(true,true);
    check(!w.startup_ready(),"first 100ms video gap blocks startup");
    check(w.evictable_bytes(1999)==0,"eviction guard prevents early unlink");
    check(w.evictable_bytes(2000)==0,"eviction guard exact boundary");
    check(!w.commit_eviction(T::Audio,0,3499),"within protected playing window");
    // A long video must not accumulate gigabytes across successive segments.
    check(w.clear_epoch(),"reset before long-form stress playback");
    const std::uint64_t video_chunk = 1536*1024;
    const std::uint64_t audio_chunk = 128*1024;
    for (std::uint32_t i=0; i<1200; ++i) {
        const auto start=3000ULL*i;
        // The consumer has advanced; unlink verified, unpinned old parts
        // before admitting the next high-bitrate 720p segment.
        if (i>2) {
            const auto old=i-3;
            check(w.commit_eviction(T::Video,old,start),"old video unlinked");
            check(w.commit_eviction(T::Audio,old,start),"old AAC unlinked");
        }
        check(w.add(T::Video,i,start,start+3000,video_chunk)==R::Accepted,
              "rolling 720p video segment admitted");
        check(w.add(T::Audio,i,start,start+3000,audio_chunk)==R::Accepted,
              "rolling AAC segment admitted");
        check(w.used_bytes()<=W::kCapacityBytes,"long video never exceeds rolling quota");
    }
    check(w.high_water_bytes()<=W::kCapacityBytes && w.pieces()<=8,
          "1200 segments (~one hour) remain within a fixed small window");
    // Eviction must not reset per-track ordering until an explicit seek.
    check(w.commit_eviction(T::Video,1197,3601000),"last old video released");
    check(w.add(T::Video,1197,3591000,3594000,video_chunk)==R::OutOfOrder,
          "evicted segment number cannot be replayed in the same epoch");
    check(w.clear_epoch(),"seek resets per-track sequence guard");
    check(w.add(T::Video,0,0,3000,video_chunk)==R::Accepted,
          "new seek epoch starts fresh from segment zero");
    check(w.clear_epoch(),"startup latch case reset");
    check(w.add(T::Muxed,0,0,3000,video_chunk)==R::Accepted,
          "new muxed stream begins from zero");
    w.set_codec_verification(true,true);
    check(w.startup_ready(),"codec-verified initial muxed prefix latches readiness");
    check(w.commit_eviction(T::Muxed,0,6000) &&
          w.buffered_video_ms()==0 && w.startup_ready(),
          "evicting the opening clip does not revert active stream to unprimed");
    check(w.begin_epoch(120000) && !w.startup_ready(),
          "seeking resets initial-prefix latch and demands new segment checks");

    if (errors) std::cerr << errors << " failures\n";
    return errors ? 1 : 0;
}
