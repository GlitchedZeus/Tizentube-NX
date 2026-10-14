#include "tizentube_nx/playback/player_volume.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main() {
    using ttnx::playback::PlayerVolume;
    PlayerVolume volume;
    static_assert(PlayerVolume::kMinimum==0);
    static_assert(PlayerVolume::kMaximum==150);
    assert(volume.percent()==130);
    assert(volume.adjust(-10)&&volume.percent()==120);
    assert(volume.adjust(10)&&volume.percent()==130);
    assert(volume.adjust(10)&&volume.percent()==140);
    assert(volume.adjust(10)&&volume.percent()==150);
    assert(!volume.adjust(10)&&volume.percent()==150);
    assert(!volume.adjust(20)&&!volume.adjust(-20));
    for(int i=0;i<20;++i)(void)volume.adjust(-10);
    assert(volume.percent()==0);
    assert(!volume.adjust(-10)&&volume.percent()==0);
    assert(volume.set(100)&&volume.percent()==100);
    assert(!volume.set(151)&&!volume.set(-1));
    assert(volume.percent()==100);
    // A newly created activity resets to the boosted default. Re-creating
    // only libmpv within the same activity must NOT create a new policy.
    const PlayerVolume new_activity;
    assert(new_activity.percent()==130);
}
