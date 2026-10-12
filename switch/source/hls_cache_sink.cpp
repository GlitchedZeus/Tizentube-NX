#ifdef TTNX_M3_MEDIA_PLANE
#include "hls_cache_sink.hpp"
#include "tizentube_nx/playback/hls_event_playlist.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include "tizentube_nx/playback/aac_mp4_structure.hpp"

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
#ifdef TTNX_M3_CACHE_HOST_TEST
constexpr const char* kParent="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX";
constexpr const char* kMediaDir="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache";
constexpr const char* kRoot="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls";
constexpr const char* kPending="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/pending";
constexpr const char* kReady="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/ready";
constexpr const char* kPrevious="/tmp/tizentube-m3-hls-test/switch/TizenTube-NX/media-cache/hls/previous";
#else
constexpr const char* kParent="sdmc:/switch/TizenTube-NX";
constexpr const char* kMediaDir="sdmc:/switch/TizenTube-NX/media-cache";
constexpr const char* kRoot="sdmc:/switch/TizenTube-NX/media-cache/hls";
constexpr const char* kPending="sdmc:/switch/TizenTube-NX/media-cache/hls/pending";
constexpr const char* kReady="sdmc:/switch/TizenTube-NX/media-cache/hls/ready";
constexpr const char* kPrevious="sdmc:/switch/TizenTube-NX/media-cache/hls/previous";
#endif

bool directory(const char* path) {
    if(mkdir(path,0700)==0)return true;
    if(errno!=EEXIST)return false;
    struct stat entry{};
    return lstat(path,&entry)==0 && S_ISDIR(entry.st_mode);
}
bool known_filename(std::string_view file) {
    if(file=="master.m3u8" || file=="video.m3u8" ||
       file=="audio.m3u8" || file=="external_audio.m4a" ||
       file=="audio_mode.txt" || file=="external_audio.bytes" ||
       file=="video_event.m3u8" || file=="video_event.tmp" ||
       file=="v_init.m4s" || file=="a_init.m4s")return true;
    // Recognize only our fixed numbered media names: v_00000.ts or
    // v_00000.m4s. No user-supplied filename enters deletion.
    if(file.size()!=10 && file.size()!=11)return false;
    if(file[0]!='v' && file[0]!='a')return false;
    if(file[1]!='_' ||
       (file.substr(7)!=".m4s" && file.substr(7)!=".ts"))return false;
    for(std::size_t i=2;i<7;++i)
        if(file[i]<'0'||file[i]>'9')return false;
    return true;
}
bool discard_known_directory(const char* path) {
    // opendir() follows symlinks by default. Verify the target is an actual
    // directory before touching any path found below it.
    struct stat root{};
    if(lstat(path,&root)!=0)return errno==ENOENT;
    if(!S_ISDIR(root.st_mode))return false;
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
// A completed HTTP 206 transfer and exact file length do not prove that
// external AAC is a usable MP4 container. Stream bounded top-level boxes
// from the closed SD file before the entire HLS clip becomes visible.
// This intentionally does not claim codec decode or A/V synchronization.
bool valid_external_aac(const std::string& path,std::uint64_t expected) {
    playback::AacMp4Structure structure;
    if(!structure.begin(expected,128ULL*1024*1024))return false;
    auto* file=std::fopen(path.c_str(),"rb");
    if(!file)return false;
    char buffer[65536];
    std::uint64_t total=0;
    bool valid=true;
    while(total<expected) {
        const auto wanted=static_cast<std::size_t>(
            std::min<std::uint64_t>(sizeof(buffer),expected-total));
        const auto got=std::fread(buffer,1,wanted,file);
        if(got!=wanted ||
           !structure.append(std::string_view(buffer,got))) {
            valid=false;break;
        }
        total+=got;
    }
    if(std::fclose(file)!=0)valid=false;
    return valid && total==expected && structure.complete();
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
std::string piece_name(bool audio,bool init,std::size_t index,bool is_ts) {
    const char prefix=audio?'a':'v';
    if(init)return std::string(1,prefix)+"_init.m4s";
    char filename[32]{};
    std::snprintf(filename,sizeof(filename),"%c_%05u.%s",
                  prefix,static_cast<unsigned>(index),is_ts?"ts":"m4s");
    return filename;
}
std::string playlist_text(const playback::HlsMediaPlaylist& source,bool audio,bool is_ts) {
    std::uint64_t max_duration=0;
    for(const auto& piece:source.segments)
        max_duration=std::max(max_duration,piece.duration_ms);
    const auto target=(max_duration+999)/1000;
    std::string out="#EXTM3U\n#EXT-X-VERSION:7\n"
                    "#EXT-X-PLAYLIST-TYPE:VOD\n"
                    "#EXT-X-TARGETDURATION:"+std::to_string(target)+"\n"
                    "#EXT-X-MEDIA-SEQUENCE:0\n";
    if(source.init_segment_url)
        out+="#EXT-X-MAP:URI=\""+piece_name(audio,true,0,false)+"\"\n";
    for(std::size_t i=0;i<source.segments.size();++i) {
        const auto ms=source.segments[i].duration_ms;
        const auto fraction=ms%1000;
        char duration[32]{};
        std::snprintf(duration,sizeof(duration),"%llu.%03llu",
                      static_cast<unsigned long long>(ms/1000),
                      static_cast<unsigned long long>(fraction));
        out+="#EXTINF:"+std::string(duration)+",\n"+
             piece_name(audio,false,i,is_ts)+"\n";
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
         std::to_string(s.bandwidth?s.bandwidth:1500000);
    if(!s.direct_media_playlist) {
        out+=",RESOLUTION="+std::to_string(s.width)+"x"+
             std::to_string(s.height)+",FRAME-RATE="+
             std::to_string(s.fps)+".000,CODECS=\"avc1.64001f,mp4a.40.2\"";
    }
    // Direct media-playlist source has no master codec/size metadata.
    // Omit unknown attributes rather than lying to the mpv demuxer.
    if(plan.audio_segments)out+=",AUDIO=\"aac\"";
    out+="\nvideo.m3u8\n";
    return out;
}
} // namespace

bool SwitchHlsCacheSink::discard_session_media() noexcept {
    if(playback::HlsPlaybackLease::is_active())return false;
    // This whitelist is deliberately narrower than the overall app folder.
    // Do not remove a file/dir with an unexpected name or type.
    const bool pending=discard_known_directory(kPending);
    const bool previous=discard_known_directory(kPrevious);
    const bool ready=discard_known_directory(kReady);
    if(pending && previous && ready) {
        // Remove our now-empty HLS root if possible, but never interpret
        // a non-empty root as permission to recurse outside known entries.
        if(rmdir(kRoot)!=0 && errno!=ENOENT && errno!=ENOTEMPTY)
            return false;
    }
    return pending && previous && ready;
}
bool SwitchHlsCacheSink::begin(const playback::HlsProbePlan& plan) {
    if(playback::HlsPlaybackLease::is_active())return false;
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
    video_written_.assign(plan.video_segments.segments.size(),false);
    audio_written_.assign(plan.audio_segments?plan.audio_segments->segments.size():0,false);
    video_init_needed_=plan.video_segments.init_segment_url.has_value();
    audio_init_needed_=plan.audio_segments && plan.audio_segments->init_segment_url.has_value();
    video_init_written_=audio_init_written_=false;
    video_ext_=plan.video_segments.init_segment_url?"m4s":"ts";
    audio_ext_=(plan.audio_segments &&
                !plan.audio_segments->init_segment_url)?"ts":"m4s";
    staged_=true;
    wrote_video_=false;
    external_audio_bytes_=0;
    return true;
}
bool SwitchHlsCacheSink::write_segment(
    bool audio,bool init,std::size_t index,std::string_view bytes) {
    if(!staged_ || bytes.empty() || bytes.size()>16*1024*1024 ||
       index>=4096)return false;
    auto& written=audio?audio_written_:video_written_;
    bool& init_written=audio?audio_init_written_:video_init_written_;
    const bool needs_init=audio?audio_init_needed_:video_init_needed_;
    if(init) {
        if(!needs_init || init_written || index!=0)return false;
    } else if(index>=written.size() || written[index])return false;
    const bool ts=audio?audio_ext_=="ts":video_ext_=="ts";
    const auto file=std::string(kPending)+"/"+piece_name(audio,init,index,ts);
    if(!persist(file,bytes))return false;
    if(init)init_written=true;
    else written[index]=true;
    if(!audio)wrote_video_=true;
    return true;
}
bool SwitchHlsCacheSink::append_external_aac(std::string_view bytes) {
    if(!staged_ || bytes.empty() ||
       bytes.size()>playback::MediaChunkReader::kMaxChunkBytes ||
       external_audio_bytes_>128ULL*1024*1024 ||
       bytes.size()>128ULL*1024*1024-external_audio_bytes_)
        return false;
    const std::string file=std::string(kPending)+"/external_audio.m4a";
    auto* out=std::fopen(file.c_str(),"ab");
    if(!out)return false;
    const bool okay=std::fwrite(bytes.data(),1,bytes.size(),out)==bytes.size() &&
                    std::fflush(out)==0 && fsync(fileno(out))==0;
    const bool closed=std::fclose(out)==0;
    if(!okay || !closed)return false;
    external_audio_bytes_+=bytes.size();
    return true;
}

bool SwitchHlsCacheSink::stage_startup_event(
    const playback::HlsProbePlan& plan,std::size_t prefix_count) {
    if(!staged_ || prefix_count==0 ||
       prefix_count>video_written_.size() ||
       video_written_.size()!=plan.video_segments.segments.size() ||
       (video_init_needed_ && !video_init_written_) ||
       plan.audio_segments ||
       (!plan.source.external_aac_url.empty() &&
        external_audio_bytes_<1024) ||
       !std::all_of(video_written_.begin(),
                    video_written_.begin()+prefix_count,
                    [](bool has_bytes){return has_bytes;}))
        return false;
    const auto snapshot=playback::make_hls_event_snapshot(
        plan.video_segments,prefix_count,false,video_ext_=="ts");
    if(!snapshot)return false;
    const auto temp=std::string(kPending)+"/video_event.tmp";
    const auto ready=std::string(kPending)+"/video_event.m3u8";
    if(!persist(temp,*snapshot))return false;
    // Readers never see a truncated playlist: first fsync, then rename.
    if(rename(temp.c_str(),ready.c_str())!=0) {
        (void)unlink(temp.c_str());
        return false;
    }
    return true;
}

bool SwitchHlsCacheSink::publish(const playback::HlsProbePlan& plan) {
    if(playback::HlsPlaybackLease::is_active())return false;
    if(!staged_ || !wrote_video_ ||
       video_written_.size()!=plan.video_segments.segments.size() ||
       video_written_.empty() ||
       !std::all_of(video_written_.begin(),video_written_.end(),[](bool b){return b;}) ||
       (video_init_needed_ && !video_init_written_) ||
       (plan.audio_segments &&
        (audio_written_.size()!=plan.audio_segments->segments.size() ||
         !std::all_of(audio_written_.begin(),audio_written_.end(),[](bool b){return b;}) ||
         (audio_init_needed_ && !audio_init_written_))))return false;
    const bool external=!plan.audio_segments &&
                        !plan.source.external_aac_url.empty();
    if(!external && external_audio_bytes_!=0)return false;
    if(external) {
        struct stat audio{};
        const std::string file=std::string(kPending)+"/external_audio.m4a";
        if(external_audio_bytes_<1024 ||
           external_audio_bytes_>128ULL*1024*1024 ||
           lstat(file.c_str(),&audio)!=0 ||
           !S_ISREG(audio.st_mode) ||
           static_cast<std::uint64_t>(audio.st_size)!=external_audio_bytes_ ||
           !valid_external_aac(file,external_audio_bytes_))
            return false;
    }
    // The incomplete/event timeline is never exposed as a finished VOD.
    // Active future streaming will require a stable, separate directory.
    const auto staged_event=std::string(kPending)+"/video_event.m3u8";
    const auto staged_temp=std::string(kPending)+"/video_event.tmp";
    if((unlink(staged_event.c_str())!=0 && errno!=ENOENT) ||
       (unlink(staged_temp.c_str())!=0 && errno!=ENOENT))return false;
    // Commit a byte-exact external AAC length, so torn post-publish files
    // cannot be mistaken for complete audio simply by having an MP4 header.
    if(external && !persist(std::string(kPending)+"/external_audio.bytes",
                            std::to_string(external_audio_bytes_)+"\n"))return false;
    // Same-transaction sidecar: do not silently treat a missing AAC as muxed.
    const char* mode=external?"external\n":
                     plan.audio_segments?"playlist\n":"muxed\n";
    if(!persist(std::string(kPending)+"/audio_mode.txt",mode))return false;
    if(!persist(std::string(kPending)+"/video.m3u8",
                playlist_text(plan.video_segments,false,video_ext_=="ts")))return false;
    if(plan.audio_segments &&
       !persist(std::string(kPending)+"/audio.m3u8",
                playlist_text(*plan.audio_segments,true,audio_ext_=="ts")))return false;
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
    external_audio_bytes_=0;
    video_written_.clear();audio_written_.clear();
    video_init_needed_=audio_init_needed_=false;
    video_init_written_=audio_init_written_=false;
}
} // namespace ttnx::switch_app
#endif // TTNX_M3_MEDIA_PLANE
