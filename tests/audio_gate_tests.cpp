#include "tizentube_nx/playback/audio_gate.hpp"
#include <cstdlib>
#include <iostream>
using Gate=ttnx::playback::AudioSelectionGate;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) {
        if(!ok){++failures;std::cerr<<"FAIL: "<<message<<"\n";}
    };
    Gate gate;
    check(gate.state()==Gate::State::Idle &&
          gate.observe(false,0)==Gate::State::Idle,
          "no AAC readiness probe before playback starts");

    gate.begin(1000);
    check(gate.observe(false,1000)==Gate::State::Checking &&
          gate.observe(false,4999)==Gate::State::Checking,
          "a stalled UI or sleeping console must not fail AAC prematurely");
    check(gate.observe(true,5000)==Gate::State::Ready &&
          gate.observe(false,100000)==Gate::State::Ready,
          "later selected audio survives an inactive clock gap");

    gate.begin(2000);
    check(gate.observe(false,2000)==Gate::State::Checking,
          "replay starts a fresh audio-readiness window");
    for(std::uint64_t i=1;i<40;++i) {
        check(gate.observe(false,2000+i*100)==Gate::State::Checking,
              "missing track waits through 3.9 seconds of active UI polling");
    }
    check(gate.observe(false,6000)==Gate::State::Missing &&
          gate.observe(true,6100)==Gate::State::Missing,
          "four seconds of active polls fails closed and latches failure");

    gate.reset();
    gate.begin(25000);
    check(gate.observe(false,24000)==Gate::State::Checking &&
          gate.observe(false,1000000)==Gate::State::Checking,
          "clock regression or hours of suspended activity do not fail");
    check(gate.observe(true,1000001)==Gate::State::Ready,
          "AAC can become selected immediately after waking");

    gate.reset();
    gate.begin(100);
    check(gate.observe(true,100)==Gate::State::Ready,
          "stop/replay clears prior missing AAC failure");
    if(failures)std::cerr<<failures<<" audio readiness regressions\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
