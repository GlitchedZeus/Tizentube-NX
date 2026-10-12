#include "m3_diagnostics.hpp"
#if defined(TTNX_M3_INTEGRATED) || defined(TTNX_M3_DIAG_HOST_TEST)
#include <chrono>
#include <cstdio>
#include <mutex>
#include <sys/stat.h>
#include <cerrno>
#include <unistd.h>

namespace ttnx::switch_app {
namespace {
std::mutex log_mutex;
bool log_active=false;
std::uint64_t session_count=0;
const char* stage_name(M3Stage type) noexcept {
    switch(type) {
        case M3Stage::Session:return "SESSION";
        case M3Stage::Source:return "SOURCE";
        case M3Stage::Hls:return "HLS";
        case M3Stage::Direct:return "DIRECT";
        case M3Stage::Transfer:return "TRANSFER";
        case M3Stage::Cache:return "CACHE";
        case M3Stage::Player:return "PLAYER";
        case M3Stage::Decode:return "DECODE";
        case M3Stage::Render:return "RENDER";
        case M3Stage::Ui:return "UI";
    }
    return "UNKNOWN";
}
const char* event_name(M3Event code) noexcept {
    switch(code) {
        case M3Event::Start:return "START";
        case M3Event::Ready:return "READY";
        case M3Event::Pass:return "PASS";
        case M3Event::Fail:return "FAIL";
        case M3Event::Refused:return "REFUSED";
        case M3Event::Selected:return "SELECTED";
        case M3Event::Begin:return "BEGIN";
        case M3Event::End:return "END";
        case M3Event::Cancel:return "CANCEL";
        case M3Event::Retry:return "RETRY";
        case M3Event::Status:return "STATUS";
        case M3Event::Progress:return "PROGRESS";
        case M3Event::Loaded:return "LOADED";
        case M3Event::Restart:return "RESTART";
        case M3Event::NoVideo:return "NO_VIDEO";
        case M3Event::NoAudio:return "NO_AUDIO";
        case M3Event::Frame:return "FRAME";
        case M3Event::Blank:return "BLANK";
        case M3Event::Presented:return "PRESENTED";
        case M3Event::Draw:return "DRAW";
        case M3Event::Seek:return "SEEK";
        case M3Event::Pause:return "PAUSE";
        case M3Event::Stop:return "STOP";
        case M3Event::Error:return "ERROR";
        case M3Event::Warning:return "WARNING";
    }
    return "UNKNOWN";
}
bool ensure_dir(const char* path) noexcept {
    if(mkdir(path,0700)==0)return true;
    if(errno!=EEXIST)return false;
    struct stat st{};
    return stat(path,&st)==0 && S_ISDIR(st.st_mode);
}
bool ensure_root() noexcept {
#ifdef TTNX_M3_DIAG_HOST_TEST
    return ensure_dir("/tmp/ttnx-m3-diag-test");
#else
    return ensure_dir("sdmc:/switch") &&
           ensure_dir("sdmc:/switch/TizenTube-NX");
#endif
}
long monotonic_ms() noexcept {
    using namespace std::chrono;
    return static_cast<long>(duration_cast<milliseconds>(
       steady_clock::now().time_since_epoch()).count()&0x7fffffff);
}
void append_line(M3Stage stage,M3Event event,
                 std::int64_t a,std::int64_t b,std::int64_t c) noexcept {
    std::FILE* file=std::fopen(M3Diagnostics::kPath,"ab+");
    if(!file){log_active=false;return;}
    if(std::fseek(file,0,SEEK_END)!=0){std::fclose(file);log_active=false;return;}
    const long size=std::ftell(file);
    if(size<0){std::fclose(file);log_active=false;return;}
    if(size>=M3Diagnostics::kMaxBytes) {
        std::fclose(file);
        (void)std::remove(M3Diagnostics::kPrevious);
        if(std::rename(M3Diagnostics::kPath,
                       M3Diagnostics::kPrevious)!=0){log_active=false;return;}
        file=std::fopen(M3Diagnostics::kPath,"wb");
        if(!file){log_active=false;return;}
        (void)std::fprintf(file,"M3DIAG version=1 rotated=1\n");
    }
    // Only compiled-in stage/event names and integers. Never an arbitrary
    // printf format, source message, network URL, ID or mpv log string.
    (void)std::fprintf(file,"%ld %s %s %lld %lld %lld\n",
        monotonic_ms(),stage_name(stage),event_name(event),
        static_cast<long long>(a),static_cast<long long>(b),
        static_cast<long long>(c));
    if(std::fflush(file)!=0 || fsync(fileno(file))!=0)log_active=false;
    std::fclose(file);
}
}
bool M3Diagnostics::begin_session() noexcept {
    std::lock_guard<std::mutex> guard(log_mutex);
    if(!ensure_root()){log_active=false;return false;}
    log_active=true;
    ++session_count;
    append_line(M3Stage::Session,M3Event::Start,
                static_cast<std::int64_t>(session_count),1,0);
    return log_active;
}
void M3Diagnostics::record(M3Stage stage,M3Event code,
                           std::int64_t a,std::int64_t b,
                           std::int64_t c) noexcept {
    std::lock_guard<std::mutex> guard(log_mutex);
    if(!log_active)return;
    append_line(stage,code,a,b,c);
}
bool M3Diagnostics::available() noexcept {
    std::lock_guard<std::mutex> guard(log_mutex);
    return log_active;
}
} // namespace ttnx::switch_app
#endif
