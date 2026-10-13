#include "tizentube_nx/playback/rolling_epoch_clock.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
using ttnx::playback::RollingEpochClock;
int main(){
    RollingEpochClock clock;
    assert(!clock.observe(0));
    assert(!clock.reset(0,0));
    assert(!clock.reset(1000,1000));
    assert(!clock.reset(0,RollingEpochClock::kMaximumVodMs+1));
    assert(clock.reset(0,630000));
    assert(!clock.anchored());
    assert(!clock.observe(std::numeric_limits<double>::quiet_NaN()));
    assert(!clock.observe(std::numeric_limits<double>::infinity()));
    assert(!clock.observe(-1));
    assert(clock.observe(0));
    assert(clock.observe(3.25));
    assert(clock.relative_ms()==3250);
    assert(clock.absolute_ms()==3250);
    assert(clock.observe(3.25)); // pause is not a new fetch window
    assert(!clock.observe(2.5)); // stale/seeked-back time-pos rejected
    assert(clock.relative_ms()==3250);
    assert(clock.observe(9));
    assert(clock.relative_ms()==9000);
    // MPEG-TS source clock can retain absolute packet timestamps.
    assert(clock.reset(180000,630000));
    assert(clock.observe(180.0));
    assert(clock.absolute_ms()==180000);
    assert(clock.relative_ms()==0);
    assert(clock.observe(183.0));
    assert(clock.absolute_ms()==183000);
    assert(clock.relative_ms()==3000);
    assert(!clock.observe(180.2));
    assert(clock.absolute_ms()==183000);
    // Fresh decoder can instead reset time-pos to zero.
    assert(clock.reset(240000,630000));
    assert(clock.observe(0.5));
    assert(clock.observe(8.5));
    assert(clock.relative_ms()==8000);
    assert(clock.absolute_ms()==248000);
    assert(!clock.observe(500.0)); // beyond remaining VOD duration
    assert(clock.absolute_ms()==248000);
    assert(clock.reset(627000,630000));
    assert(clock.observe(0));
    assert(clock.observe(3.0));
    assert(!clock.observe(3.001));
    assert(clock.absolute_ms()==630000);
    // >100 re-prime epochs must anchor independently; absolute time is
    // source-derived, not a sum of lossy per-epoch player timestamps.
    for(unsigned i=0;i<105;++i){
        const auto epoch=static_cast<std::uint64_t>(i)*6000;
        assert(clock.reset(epoch,630000));
        assert(clock.observe(i%2?static_cast<double>(epoch)/1000.0:0));
        assert(clock.observe(i%2?static_cast<double>(epoch)/1000.0+3:3));
        assert(clock.absolute_ms()==epoch+3000);
    }
}
