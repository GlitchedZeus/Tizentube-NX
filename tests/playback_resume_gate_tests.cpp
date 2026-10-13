#include "tizentube_nx/library/playback_resume_gate.hpp"
#include <cassert>
using ttnx::library::PlaybackResumeGate;
int main() {
    PlaybackResumeGate zero;
    std::uint32_t output=999;
    assert(!zero.offer(true,true,false,500,output));
    assert(zero.can_checkpoint(0));
    PlaybackResumeGate selected(603);
    assert(!selected.offer(false,true,false,1200,output));
    assert(!selected.offer(true,false,false,1200,output));
    assert(!selected.can_checkpoint(0));
    assert(selected.offer(true,true,false,1200,output)&&output==603);
    assert(!selected.can_checkpoint(0));
    selected.acknowledge(true);
    assert(!selected.can_checkpoint(2));
    assert(!selected.can_checkpoint(500));
    assert(selected.can_checkpoint(594));
    assert(selected.can_checkpoint(603));
    assert(!selected.offer(true,true,false,1200,output));
    PlaybackResumeGate almost_done(1199);
    assert(almost_done.offer(true,true,false,1200,output)&&output==1195);
    PlaybackResumeGate failed(500);
    assert(failed.offer(true,true,false,900,output));
    failed.acknowledge(false);
    assert(!failed.can_checkpoint(0));
    assert(!failed.can_checkpoint(10)); // do not destroy a saved checkpoint
    PlaybackResumeGate rolling(600);
    assert(!rolling.offer(true,true,true,900,output));
    assert(!rolling.can_checkpoint(1));
    return 0;
}
