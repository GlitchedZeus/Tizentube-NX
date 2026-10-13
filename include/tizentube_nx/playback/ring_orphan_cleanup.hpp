#pragma once
#include "tizentube_nx/playback/rolling_file_stream.hpp"
#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace ttnx::playback {
// Startup ONLY, before creating any network worker or libmpv reader.
// Removes exactly one app-owned, fixed-name file; never scans user SD data.
struct RingOrphanCleanup final {
    static constexpr const char* kFilename="m4_stream_ring.part";
    [[nodiscard]] static bool at_startup(const std::string& app_directory,
                                         bool no_live_decoder) {
        if(!no_live_decoder || app_directory.empty())return false;
        struct stat parent{};
        if(::lstat(app_directory.c_str(),&parent)!=0 ||
           !S_ISDIR(parent.st_mode))return false;
        const std::string path=app_directory+"/"+kFilename;
        struct stat file{};
        if(::lstat(path.c_str(),&file)!=0)return errno==ENOENT;
        if(!S_ISREG(file.st_mode) || file.st_nlink!=1 ||
           file.st_size<0 ||
           static_cast<std::uint64_t>(file.st_size)>
               RollingFileStream::kMaximumMedia)return false;
        return ::unlink(path.c_str())==0;
    }
};
} // namespace ttnx::playback
