#include "m3_diagnostics.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
int main() {
    using namespace ttnx::switch_app;
    int fail=0;
    auto check=[&](bool ok,const char* label) {
        if(!ok){++fail;std::cerr<<"FAIL "<<label<<"\n";}
    };
    fs::remove_all("/tmp/ttnx-m3-diag-test");
    check(M3Diagnostics::begin_session(),"diagnostics create SD test directory");
    M3Diagnostics::record(M3Stage::Hls,M3Event::Fail,403,3,0);
    M3Diagnostics::record(M3Stage::Decode,M3Event::NoVideo,0,1,2);
    std::ifstream file(M3Diagnostics::kPath);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    check(text.find("HLS FAIL 403 3 0")!=std::string::npos,
          "failed HTTP category persisted");
    check(text.find("DECODE NO_VIDEO 0 1 2")!=std::string::npos,
          "video diagnostics persisted without signed URLs");
    check(text.find("googlevideo")==std::string::npos &&
          text.find("visitor")==std::string::npos &&
          text.find("cookie")==std::string::npos &&
          text.find("https://")==std::string::npos,
          "fixed typed API cannot emit bearer URLs or headers");
    for(int i=0;i<5200;++i)
        M3Diagnostics::record(M3Stage::Render,M3Event::Frame,i,0,0);
    check(fs::file_size(M3Diagnostics::kPath) < M3Diagnostics::kMaxBytes+256 &&
          fs::exists(M3Diagnostics::kPrevious),
          "bounded log rotates instead of filling SD indefinitely");
    check(M3Diagnostics::begin_session(),"second session appends safely");
    fs::remove_all("/tmp/ttnx-m3-diag-test");
    return fail?EXIT_FAILURE:EXIT_SUCCESS;
}
