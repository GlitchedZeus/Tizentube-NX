#include "rolling_hls_sink.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

// Host-only integration harness. All video bytes are locally generated in the
// accompanying Python test. The accepted Switch player remains untouched.
namespace fs = std::filesystem;
using namespace ttnx::switch_app;
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const fs::path samples = argv[1], control = argv[2];
    RollingHlsSink sink;
    if (!sink.begin(2000)) return 3;
    bool cancelled = false;
    bool retired = false;
    const auto maybe_retire = [&]() {
        if(!retired && fs::exists(control / "retire_first") &&
           sink.active_segments()>=2) {
            if(!sink.retire_before(1))return false;
            retired=true;
            std::cout << "retired=0" << std::endl;
        }
        return true;
    };
    for (int i=0; i<6; ++i) {
        if (fs::exists(control / "cancel")) {cancelled=true;break;}
        char name[32]{};
        std::snprintf(name,sizeof(name),"media_%05d.ts",i);
        std::ifstream file(samples / name,std::ios::binary);
        if (!file) return 4;
        const std::string bytes{std::istreambuf_iterator<char>(file), {}};
        if (!sink.append(static_cast<std::size_t>(i),2000,bytes)) return 5;
        std::cout << "segment=" << i << std::endl;
        for(int j=0;j<9;++j) {
            if(!maybe_retire())return 8;
            if (fs::exists(control / "cancel")) {cancelled=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(cancelled)break;
    }
    if (!cancelled && !sink.finish()) return 6;
    // Playback owns the life cycle. No media may be deleted before the
    // decoder has closed and the test signals confirmation.
    bool permitted=false;
    for(int i=0;i<300;++i) {
        if(!maybe_retire())return 8;
        if(fs::exists(control / "decoder_closed")) {permitted=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!permitted || !sink.close_session()) return 7;
    std::cout << "removed_all_media" << std::endl;
    return 0;
}
