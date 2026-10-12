#if defined(TTNX_M4_ROLLING_HOST_TEST) || defined(TTNX_M4_ROLLING_STREAM)
#include "rolling_hls_sink.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ttnx::switch_app {
namespace {
#ifdef TTNX_M4_ROLLING_HOST_TEST
constexpr const char* roots[]={"/tmp/ttnx-m4-rolling-test",
    "/tmp/ttnx-m4-rolling-test/switch",
    "/tmp/ttnx-m4-rolling-test/switch/TizenTube-NX",
    "/tmp/ttnx-m4-rolling-test/switch/TizenTube-NX/media-cache",
    RollingHlsSink::kRoot};
#else
constexpr const char* roots[]={"sdmc:/switch","sdmc:/switch/TizenTube-NX",
    "sdmc:/switch/TizenTube-NX/media-cache",RollingHlsSink::kRoot};
#endif
bool ancestors(bool create) {
    for(const auto* p:roots) {
        struct stat st{};
        if(lstat(p,&st)==0) {if(!S_ISDIR(st.st_mode))return false;}
        else if(errno!=ENOENT)return false;
        else if(create && mkdir(p,0700)!=0)return false;
    }
    return true;
}
std::string segment_path(std::size_t index,bool part=false) {
    char name[32]{};
    std::snprintf(name,sizeof(name),"v_%05u.ts%s",
                  static_cast<unsigned>(index),part?".part":"");
    return std::string(RollingHlsSink::kRoot)+"/"+name;
}
bool known(std::string_view name) {
    if(name=="stream.m3u8" || name=="stream.m3u8.part")return true;
    if(name.size()!=10 && name.size()!=15)return false;
    if(name.substr(0,2)!="v_" ||
       name.substr(7)!=(name.size()==10?".ts":".ts.part"))return false;
    for(std::size_t i=2;i<7;++i)if(name[i]<'0'||name[i]>'9')return false;
    return true;
}
bool regular_or_absent(const std::string& path) {
    struct stat st{};
    if(lstat(path.c_str(),&st)==0)return S_ISREG(st.st_mode);
    return errno==ENOENT;
}
bool write_exclusive(const std::string& path,std::string_view data) {
    // O_EXCL refuses replaced files/symlinks and a stale .part. Never
    // follow a user-controlled pathname when creating media.
    const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return false;
    bool ok=!data.empty();
    std::size_t done=0;
    while(ok && done<data.size()) {
        const auto n=write(fd,data.data()+done,data.size()-done);
        if(n<=0)ok=false;
        else done+=static_cast<std::size_t>(n);
    }
    if(ok)ok=fsync(fd)==0;
    if(close(fd)!=0)ok=false;
    if(!ok)(void)unlink(path.c_str());
    return ok;
}
bool remove_file(const std::string& path) {
    if(!regular_or_absent(path))return false;
    return unlink(path.c_str())==0 || errno==ENOENT;
}
}
bool RollingHlsSink::discard() noexcept {
    if(playback::HlsPlaybackLease::is_active() || !ancestors(false))return false;
    struct stat st{};
    if(lstat(kRoot,&st)!=0)return errno==ENOENT;
    if(!S_ISDIR(st.st_mode))return false;
    DIR* dir=opendir(kRoot);
    if(!dir)return false;
    std::vector<std::string> paths;
    bool ok=true;
    for(auto* e=readdir(dir);e;e=readdir(dir)) {
        const std::string name=e->d_name;
        if(name=="."||name=="..")continue;
        if(paths.size()>kMaxSegments*2+2 || !known(name)) {ok=false;break;}
        const std::string path=std::string(kRoot)+"/"+name;
        struct stat file{};
        if(lstat(path.c_str(),&file)!=0 || !S_ISREG(file.st_mode)) {
            ok=false;break;
        }
        paths.push_back(path);
    }
    if(closedir(dir)!=0)ok=false;
    if(!ok)return false; // global preflight BEFORE deletion
    for(const auto& path:paths)if(!remove_file(path))return false;
    return rmdir(kRoot)==0 || errno==ENOENT;
}
bool RollingHlsSink::close_session() noexcept {
    if(!discard())return false;
    segments_.clear();
    bytes_=0;
    next_=0;
    target_seconds_=0;
    started_=false;
    finished_=false;
    return true;
}
bool RollingHlsSink::begin(std::uint64_t maximum_segment_ms) {
    if(!maximum_segment_ms || maximum_segment_ms>60000)return false;
    if(started_ || playback::HlsPlaybackLease::is_active() ||
       !discard() || !ancestors(true))return false;
    segments_.clear();bytes_=0;next_=0;
    target_seconds_=(maximum_segment_ms+999)/1000;started_=true;
    finished_=false;
    return true;
}
bool RollingHlsSink::append(std::size_t index,std::uint64_t duration_ms,
                            std::string_view verified_muxed_ts) {
    if(!started_ || finished_ || !ancestors(false) || index!=next_ ||
       index>=kMaxSegments || duration_ms==0 ||
       duration_ms>(target_seconds_*1000) ||
       verified_muxed_ts.empty() || verified_muxed_ts.size()>16*1024*1024 ||
       verified_muxed_ts.size()>kMaxMediaBytes-bytes_)return false;
    const auto tmp=segment_path(index,true), ready=segment_path(index);
    if(!regular_or_absent(tmp) || !regular_or_absent(ready) ||
       !write_exclusive(tmp,verified_muxed_ts))return false;
    if(rename(tmp.c_str(),ready.c_str())!=0) {
        (void)remove_file(tmp);return false;
    }
    segments_.push_back(Segment{index,duration_ms,verified_muxed_ts.size()});
    bytes_+=verified_muxed_ts.size();++next_;
    if(publish(false))return true;
    // Roll back an unpublished segment. Without this, a failed manifest
    // rename leaves untracked SD bytes that can bypass the rolling quota.
    // Only return quota after verifying the failed-to-publish media file
    // was removed. A failed unlink poisons this session instead of silently
    // undercounting bytes in the subsequent appends.
    if(!remove_file(ready)) {
        started_=false;
        return false;
    }
    segments_.pop_back();
    bytes_-=verified_muxed_ts.size();
    --next_;
    return false;
}
bool RollingHlsSink::publish(bool end_list) {
    if(segments_.empty())return false;
    std::string text="#EXTM3U\n#EXT-X-VERSION:3\n"
       "#EXT-X-TARGETDURATION:"+std::to_string(target_seconds_)+"\n"
       "#EXT-X-MEDIA-SEQUENCE:"+std::to_string(segments_.front().index)+"\n";
    for(const auto& s:segments_) {
        char millis[36]{};
        std::snprintf(millis,sizeof(millis),"%llu.%03llu",
                      static_cast<unsigned long long>(s.duration_ms/1000),
                      static_cast<unsigned long long>(s.duration_ms%1000));
        text+="#EXTINF:"+std::string(millis)+",\n"+
              "v_";
        char segment[24]{};
        std::snprintf(segment,sizeof(segment),"%05u.ts",static_cast<unsigned>(s.index));
        text+=segment; text+='\n';
    }
    if(end_list)text+="#EXT-X-ENDLIST\n";
    if(text.size()>65536 || !ancestors(false))return false;
    const auto partial=std::string(kRoot)+"/stream.m3u8.part";
    const auto ready=std::string(kRoot)+"/stream.m3u8";
    // Never follow a poisoned named manifest. Replacement behavior is
    // filesystem dependent and must be verified on actual Switch SD.
    if(!regular_or_absent(partial) || !regular_or_absent(ready) ||
       !remove_file(partial) || !write_exclusive(partial,text))return false;
    if(rename(partial.c_str(),ready.c_str())!=0) {
        (void)remove_file(partial);return false;
    }
    return true;
}
bool RollingHlsSink::retire_before(std::size_t first_index) {
    if(!started_ || first_index>next_ || segments_.empty() ||
       first_index>=next_ || !ancestors(false))return false;
    const auto first=std::find_if(segments_.begin(),segments_.end(),
                     [first_index](const Segment& s){return s.index>=first_index;});
    if(first==segments_.end() || first==segments_.begin())return false;
    // New playlist must omit old segments before any unlink can happen.
    const auto removed=std::vector<Segment>(segments_.begin(),first);
    const auto old=segments_;
    segments_.erase(segments_.begin(),first);
    if(!publish(finished_)) {segments_=old;return false;}
    for(const auto& s:removed) {
        if(!remove_file(segment_path(s.index)))return false;
        bytes_-=s.bytes;
    }
    return true;
}
bool RollingHlsSink::finish() {
    if(!started_ || finished_ || !publish(true))return false;
    finished_=true;
    return true;
}
} // namespace ttnx::switch_app

#endif // opt-in rolling HLS backend
