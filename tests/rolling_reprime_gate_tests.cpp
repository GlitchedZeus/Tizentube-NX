#include "tizentube_nx/playback/rolling_reprime_gate.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>
using ttnx::playback::RollingReprimeGate;
using Gate=RollingReprimeGate;
static Gate::Boundary boundary(std::size_t origin,std::size_t next,
                              std::size_t total,bool quota=true) {
    return {next,origin,total,quota,true,true};
}
int main() {
    for(auto why:{Gate::End::Stop,Gate::End::Error}) {
        Gate gate;
        assert(!gate.observe(why,boundary(0,2,5),true));
        assert(gate.stage()==(why==Gate::End::Stop
             ?Gate::Stage::Cancelled:Gate::Stage::Failed));
        assert(!gate.observe(Gate::End::Eof,boundary(0,2,5),true));
    }
    {
        Gate gate;
        assert(!gate.observe(Gate::End::Eof,boundary(0,2,5),false));
        assert(gate.stage()==Gate::Stage::Failed);
    }
    {
        Gate gate;
        auto b=boundary(0,2,5);b.complete_prefix=false;
        assert(!gate.observe(Gate::End::Eof,b,true));
        assert(gate.stage()==Gate::Stage::Failed);
    }
    {
        Gate gate;
        auto b=boundary(0,2,5);b.decoder_attached=false;
        assert(!gate.observe(Gate::End::Eof,b,true));
        assert(gate.stage()==Gate::Stage::Failed);
    }
    {
        Gate gate;
        assert(!gate.observe(Gate::End::Eof,boundary(0,5,5,false),true));
        assert(gate.stage()==Gate::Stage::Finished);
    }
    {
        Gate gate;
        assert(!gate.observe(Gate::End::Eof,boundary(0,1,5,false),true));
        assert(gate.stage()==Gate::Stage::Failed);
    }
    for(auto invalid:{boundary(2,2,5),boundary(3,2,5),
                       boundary(5,6,5),boundary(0,6,5)}) {
        Gate gate;
        assert(!gate.observe(Gate::End::Eof,invalid,true));
        assert(gate.stage()==Gate::Stage::Failed);
    }
    // 210 x 3s muxed segments = 10m30s. Exercise every safe handoff.
    {
        Gate gate;
        for(std::size_t index=0;index<210;index+=2) {
            auto b=boundary(index,index+2,210,index+2<210);
            const bool more=index+2<210;
            assert(gate.observe(Gate::End::Eof,b,true)==more);
            if(!more) {
                assert(gate.stage()==Gate::Stage::Finished);
                break;
            }
            assert(gate.next_segment()==index+2);
            assert(gate.stage()==Gate::Stage::Requested);
            assert(!gate.started(index));
            assert(gate.started(index+2));
            assert(gate.stage()==Gate::Stage::WaitingForPrefix);
            assert(!gate.observe(Gate::End::Eof,b,true));
            assert(gate.prefix_ready());
            assert(!gate.prefix_ready());
            assert(gate.stage()==Gate::Stage::Watching);
        }
        assert(gate.starts_completed()==104);
        assert(!gate.observe(Gate::End::Eof,boundary(208,210,210),true));
    }
    // Repeated or rewound segment boundaries may never reissue stale URLs.
    {
        Gate gate;
        assert(gate.observe(Gate::End::Eof,boundary(0,2,6),true));
        assert(gate.started(2));
        assert(gate.prefix_ready());
        assert(!gate.observe(Gate::End::Eof,boundary(2,2,6),true));
        assert(gate.stage()==Gate::Stage::Failed);
    }
    {
        Gate gate;
        assert(gate.observe(Gate::End::Eof,boundary(0,2,6),true));
        gate.cancel();
        assert(!gate.started(2));
        assert(!gate.prefix_ready());
        gate.fail();
        assert(gate.stage()==Gate::Stage::Cancelled);
    }
}
