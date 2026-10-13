#include "tizentube_nx/playback/rolling_prefetch_window.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>
using ttnx::playback::RollingPrefetchWindow;
int main() {
    RollingPrefetchWindow g;
    assert(!g.configured() && g.can_fetch(400,0,0)); // legacy host mode
    assert(!g.configure({}));
    assert(!g.configure({0}));
    assert(!g.configure({60001}));
    assert(!g.configure(std::vector<std::uint64_t>(4097,1000)));
    assert(!g.configure(std::vector<std::uint64_t>(1000,60000)));
    assert(g.configure(std::vector<std::uint64_t>(32,3000)));
    assert(g.total_duration_ms()==96000 && g.count()==32);
    for(std::size_t i=0;i<=5;++i)assert(g.can_fetch(i,0,0));
    assert(!g.can_fetch(6,0,0));
    assert(!g.can_fetch(6,0,2999));
    assert(g.can_fetch(6,0,3000));
    assert(g.can_fetch(9,0,12000));
    assert(!g.can_fetch(10,0,12000));
    assert(!g.can_fetch(32,0,UINT64_MAX));
    assert(!g.can_fetch(0,6,UINT64_MAX));
    // Restart at a later segment: time-pos resets, the producer window must
    // be measured relative to the new epoch's first segment.
    assert(g.can_fetch(11,6,0));
    assert(!g.can_fetch(12,6,0));
    assert(g.can_fetch(12,6,3000));
    assert(g.can_fetch(31,6,UINT64_MAX));
    // Invalid configure attempts cannot corrupt an accepted plan.
    assert(!g.configure({3000,0}));
    assert(g.count()==32 && g.can_fetch(9,0,12000));
    return 0;
}
