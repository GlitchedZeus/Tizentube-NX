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
          "late async AAC track selection must not fail early");
    check(gate.observe(true,4999)==Gate::State::Ready &&
          gate.observe(false,9000)==Gate::State::Ready,
          "verified AAC track remains ready until restart");
    gate.begin(11000);
    check(gate.observe(false,14999)==Gate::State::Checking &&
          gate.observe(false,15000)==Gate::State::Missing &&
          gate.observe(true,15001)==Gate::State::Missing,
          "missing AAC fails closed after four seconds, no false recovery");
    gate.reset();
    gate.begin(25000);
    check(gate.observe(false,24000)==Gate::State::Checking &&
          gate.observe(false,27999)==Gate::State::Checking &&
          gate.observe(false,28000)==Gate::State::Missing,
          "a regressing monotonic time does not underflow to immediate fail");
    gate.reset();
    gate.begin(100);
    check(gate.observe(true,100)==Gate::State::Ready,
          "stop/replay clears stale failure and accepts immediate AAC");
    if(failures)std::cerr<<failures<<" audio readiness regressions\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
