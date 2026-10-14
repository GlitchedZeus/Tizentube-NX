#include "tizentube_nx/playback/rolling_mpv_stream_bridge.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <filesystem>
#include <thread>
#include <cstring>
#include <string>
using ttnx::playback::RollingFileStream;
using ttnx::playback::RollingMpvStreamBridge;
namespace fs=std::filesystem;
int main() {
 const auto dir=fs::temp_directory_path()/"ttnx_mpv_bridge_test";
 fs::remove_all(dir);fs::create_directory(dir);
 auto ring=RollingFileStream::create((dir/"media.bin").string(),65536);
 assert(ring);
 { RollingMpvStreamBridge bridge(ring);
 mpv_handle player{};
 assert(bridge.register_before_initialize(&player)==0);
 mpv_stream_cb_info stream{};
 char valid[]="ttnxring://media",bad[]="https://googlevideo.com/";
 assert(g_open_fn(g_user_data,bad,&stream)==MPV_ERROR_LOADING_FAILED);
 assert(g_open_fn(g_user_data,valid,&stream)==0);
 assert(stream.cookie && stream.read_fn && stream.seek_fn &&
        stream.close_fn && stream.cancel_fn && !stream.size_fn);
 const std::string text(32768,'M');
 assert(ring->append(text)==RollingFileStream::Write::Written);
 std::string copied(32768,'\0');
 assert(stream.read_fn(stream.cookie,copied.data(),copied.size())==32768);
 assert(copied==text);
 assert(stream.seek_fn(stream.cookie,32769)==MPV_ERROR_UNSUPPORTED);
 assert(stream.seek_fn(stream.cookie,0)==0);
 assert(stream.read_fn(stream.cookie,copied.data(),copied.size())==32768);
 assert(!ring->release_before(32768)); // mpv FD still open.
 std::atomic<bool> entered{false};
 std::atomic<std::int64_t> status{0};
 std::thread waiting([&](){char c{};entered=true;status=stream.read_fn(stream.cookie,&c,1);});
 while(!entered.load())std::this_thread::yield();
 stream.cancel_fn(stream.cookie);
 waiting.join();assert(status==MPV_ERROR_GENERIC);
 stream.close_fn(stream.cookie);
 assert(!ring->release_before(32768)); // cancelled epoch cannot reuse media.
 } // mpv has already been destroyed and callbacks can no longer open.
 ring.reset();assert(!fs::exists(dir/"media.bin"));
 fs::remove_all(dir);
}
