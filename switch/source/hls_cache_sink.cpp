#ifdef TTNX_M3_MEDIA_PLANE
#include "hls_cache_sink.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace ttnx::switch_app {
namespace {
constexpr const char* kParent="sdmc:/switch/TizenTube-NX";
constexpr const char* kMediaDir="sdmc:/switch/TizenTube-NX/media-cache";
constexpr const char* kRoot="sdmc:/switch/TizenTube-NX/media-cache/hls";
constexpr const char* kPending="sdmc:/switch/TizenTube-NX/media-cache/hls/pending";
constexpr const char* kReady="sdmc:/switch/TizenTube-NX/media-cache/hls/ready";
constexpr const char* kPrevious="sdmc:/switch/TizenTube-NX/media-cache/hls/previous";

bool directory(const char* path) {
    if(mkdir(path,0700)==0)return true;
    if(errno!=EEXIST)return false;
    struct stat entry{};
    return lstat(path,&entry)==0 && S_ISDIR(entry.st_mode);
}
bool known_filename(std::string_view file) {
    if(file=="master.m3u8" || file=="video.m3u8" ||
       file=="audio.m3u8" || file=="v_init.m4s" ||
       file=="a_init.m4s")return true;
    if(file.size()!=11)return false; // v_00000.m4s = 11 chars
    if(file[0]!='v' && file[0]!='a')return false;
    if(file[1]!='_' || file.substr(7)!=".m4s")return false;
    for(std::size_t i=2;i<7;++i)
        if(file[i]<'0'||file[i]>'9')return false;
    return true;
}
bool discard_known_directory(const char* path) {
    auto* directory_stream=opendir(path);
    if(!directory_stream) return errno==ENOENT;
    bool okay=true;
    std::size_t count=0;
    for(auto* d=readdir(directory_stream);d;d=readdir(directory_stream)) {
        const std::string filename=d->d_name;
        if(filename=="."||filename=="..")continue;
        if(++count>8200 || !known_filename(filename)) {
            okay=false;break;
        }
        const std::string absolute=std::string(path)+"/"+filename;
        struct stat entry{};
        if(lstat(absolute.c_str(),&entry)!=0 ||
           !S_ISREG(entry.st_mode) ||
           unlink(absolute.c_str())!=0) {
            okay=false;break;
        }
    }
    closedir(directory_stream);
    return okay && rmdir(path)==0;
}
bool persist(const std::string& path,std::string_view bytes) {
    auto* handle=std::fopen(path.c_str(),"wb");
    if(!handle)return false;
    bool okay=!bytes.empty() &&
        std::fwrite(bytes.data(),1,bytes.size(),handle)==bytes.size() &&
        std::fflush(handle)==0 &&
        fsync(fileno(handle))==0;
    if(std::fclose(handle)!=0)okay=false;
    if(!okay)(void)unlink(path.c_str());
    return okay;
}
std::string piece_name(bool audio,bool init,std::size_t index) {
    const char prefix=audio?'a':'v';
    if(init)return std::string(1,prefix)+"_init.m4s";
    char filename[32]{};
    std::snprintf(filename,sizeof(filename),"%c_%05u.m4s",
                  prefix,static_cast<unsigned>(index));
    return filename;
}
std::string playlist_text(const playback::HlsMediaPlaylist& source,bool audio) {
    std::uint64_t max_duration=0;
    for(const auto& piece:source.segments)
        max_duration=std::max(max_duration,piece.duration_ms);
    const auto target=(max_duration+999)/1000;
    std::string out="#EXTM3U\n#EXT-X-VERSION:7\n"
                    "#EXT-X-PLAYLIST-TYPE:VOD\n"
                    "#EXT-X-TARGETDURATION:"+std::to_string(target)+"\n"
                    "#EXT-X-MEDIA-SEQUENCE:0\n";
    if(source.init_segment_url)
        out+="#EXT-X-MAP:URI=\""+piece_name(audio,true,0)+"\"\n";
    for(std::size_t i=0;i<source.segments.size();++i) {
        const auto ms=source.segments[i].duration_ms;
        const auto fraction=ms%1000;
        char duration[32]{};
        std::snprintf(duration,sizeof(duration),"%llu.%03llu",
                      static_cast<unsigned long long>(ms/1000),
                      static_cast<unsigned long long>(fraction));
        out+="#EXTINF:"+std::string(duration)+",\n"+
             piece_name(audio,false,i)+"\n";
    }
    out+="#EXT-X-ENDLIST\n";
    return out;
}
std::string master_text(const playback::HlsProbePlan& plan) {
    std::string out="#EXTM3U\n#EXT-X-VERSION:7\n";
    if(plan.audio_segments) {
        out+="#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\","
             "NAME=\"Stereo AAC\",DEFAULT=YES,AUTOSELECT=YES,"
             "URI=\"audio.m3u8\"\n";
    }
    const auto& s=plan.selected;
    out+="#EXT-X-STREAM-INF:BANDWIDTH="+
         std::to_string(s.bandwidth?s.bandwidth:1500000)+
         ",RESOLUTION="+std::to_string(s.width)+"x"+
         std::to_string(s.height)+",FRAME-RATE="+
         std::to_string(s.fps)+".000,CODECS=\"avc1.64001f,mp4a.40.2\"";
    if(plan.audio_segments)out+=",AUDIO=\"aac\"";
    out+="\nvideo.m3u8\n";
    return out;
}
} // namespace

bool SwitchHlsCacheSink::begin(const playback::HlsProbePlan& plan) {
    abort();
    if(!directory(kParent) || !directory(kMediaDir) ||
       !directory(kRoot))return false;
    // Recover a completed old clip after a power loss between moving
    // ready -> previous and pending -> ready. Never throw away that old
    // completed media until ready has been restored.
    struct stat current{},backup{};
    const bool ready_exists=lstat(kReady,&current)==0;
    const bool backup_exists=lstat(kPrevious,&backup)==0;
    if(backup_exists && !S_ISDIR(backup.st_mode))return false;
    if(!ready_exists && backup_exists &&
       rename(kPrevious,kReady)!=0)return false;
    // Clear only our known app-owned failed/old scratch files, never arbitrary
    // SD-card content, and never the currently published "ready" directory.
    if(!discard_known_directory(kPending) ||
       !discard_known_directory(kPrevious) ||
       !directory(kPending))return false;
    if(plan.video_segments.segments.empty() ||
       plan.video_segments.segments.size()>4096 ||
       (plan.audio_segments && plan.audio_segments->segments.size()>4096))
        return false;
    staged_=true;
    wrote_video_=false;
    return true;
}
bool SwitchHlsCacheSink::write_segment(
    bool audio,bool init,std::size_t index,std::string_view bytes) {
    if(!staged_ || bytes.empty() || bytes.size()>16*1024*1024 ||
       index>=4096)return false;
    const auto file=std::string(kPending)+"/"+piece_name(audio,init,index);
    if(!persist(file,bytes))return false;
    if(!audio)wrote_video_=true;
    return true;
}
bool SwitchHlsCacheSink::publish(const playback::HlsProbePlan& plan) {
    if(!staged_ || !wrote_video_ ||
       plan.video_segments.segments.empty() ||
       (plan.audio_segments && plan.audio_segments->segments.empty()))
        return false;
    if(!persist(std::string(kPending)+"/video.m3u8",
                playlist_text(plan.video_segments,false)))return false;
    if(plan.audio_segments &&
       !persist(std::string(kPending)+"/audio.m3u8",
                playlist_text(*plan.audio_segments,true)))return false;
    if(!persist(std::string(kPending)+"/master.m3u8",
                master_text(plan)))return false;

    struct stat old{};
    const bool have_ready=lstat(kReady,&old)==0;
    if(have_ready && !S_ISDIR(old.st_mode))return false;
    if(have_ready && rename(kReady,kPrevious)!=0)return false;
    if(rename(kPending,kReady)!=0) {
        if(have_ready)(void)rename(kPrevious,kReady);
        return false;
    }
    staged_=false;
    // The previous completed clip is discarded only AFTER the new ready
    // directory has been fully published and its local manifest is visible.
    if(have_ready)(void)discard_known_directory(kPrevious);
    return true;
}
void SwitchHlsCacheSink::abort() noexcept {
    if(staged_)(void)discard_known_directory(kPending);
    staged_=false;
    wrote_video_=false;
}
} // namespace ttnx::switch_app
#endif // TTNX_M3_MEDIA_PLANE
