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
    if (errors) std::cerr << errors << " failures\n";
    return errors ? 1 : 0;
}
