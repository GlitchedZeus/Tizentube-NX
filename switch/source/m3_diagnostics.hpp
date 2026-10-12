#pragma once
#include <cstdint>
namespace ttnx::switch_app {

// Private, fixed-schema M3 field log. Only enums and numbers cross its
// boundary: no URL, request/response, cookie, visitor token or media bytes.
enum class M3Stage {
    Session, Source, Hls, Direct, Transfer, Cache, Player, Decode, Render, Ui
};
enum class M3Event {
    Start, Ready, Pass, Fail, Refused, Selected, Begin, End, Cancel,
    Retry, Status, Progress, Loaded, Restart, NoVideo, NoAudio,
    Frame, Blank, Presented, Draw, Seek, Pause, Stop, Error, Warning
};
class M3Diagnostics {
public:
#ifdef TTNX_M3_DIAG_HOST_TEST
    static constexpr const char* kPath="/tmp/ttnx-m3-diag-test/m3-playback.log";
    static constexpr const char* kPrevious="/tmp/ttnx-m3-diag-test/m3-playback.previous.log";
#else
    static constexpr const char* kPath="sdmc:/switch/TizenTube-NX/m3-playback.log";
    static constexpr const char* kPrevious="sdmc:/switch/TizenTube-NX/m3-playback.previous.log";
#endif
    static constexpr long kMaxBytes=192*1024;
    static bool begin_session() noexcept;
    static void record(M3Stage stage,M3Event code,
                       std::int64_t a=0,std::int64_t b=0,
                       std::int64_t c=0) noexcept;
    static bool available() noexcept;
};
} // namespace ttnx::switch_app
