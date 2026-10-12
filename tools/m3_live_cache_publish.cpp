// Real-media host integration of the production SwitchHlsCacheSink.
// Never downloads media, prints filenames, or exposes signed bearer URLs.
// Called only from the CI's disposable, private source-proof workspace.
#include "hls_cache_sink.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace {
bool mkdir_owned(const char* path) {
    if(mkdir(path,0700)!=0 && errno!=EEXIST)return false;
    struct stat entry{};
    return lstat(path,&entry)==0 && S_ISDIR(entry.st_mode);
}
bool append_audio(ttnx::switch_app::SwitchHlsCacheSink& sink,
                  const char* filename) {
    std::ifstream stream(filename,std::ios::binary);
    if(!stream)return false;
    std::vector<char> bytes(1024*1024);
    bool wrote=false;
    while(stream) {
        stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        const auto count=stream.gcount();
        if(count>0) {
            wrote=true;
            if(!sink.append_external_aac(
                 std::string_view(bytes.data(),static_cast<std::size_t>(count))))
                return false;
        }
    }
    return wrote && stream.eof() && !stream.bad();
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 1;
    char* end=nullptr;
    const unsigned long millis=std::strtoul(argv[3],&end,10);
    if(!end || end==argv[3] || *end!='\0' ||
       millis<500 || millis>120000)return 2;
    if(!mkdir_owned("/tmp/tizentube-m3-hls-test") ||
       !mkdir_owned("/tmp/tizentube-m3-hls-test/switch"))return 3;

    std::ifstream video(argv[1],std::ios::binary);
    if(!video)return 4;
    std::string segment((std::istreambuf_iterator<char>(video)),
                         std::istreambuf_iterator<char>());
    if(!video.eof() && video.bad())return 5;
    if(segment.size()<376 || segment.size()>16*1024*1024)return 6;

    ttnx::playback::HlsProbePlan plan;
    plan.source.video_id="dQw4w9WgXcQ";
    plan.source.duration_ms=millis;
    // The URL fields exist only in memory; sink writes ONLY local basenames.
    plan.source.external_aac_url="https://r1.googlevideo.com/videoplayback";
    plan.selected.direct_media_playlist=true;
    plan.selected.bandwidth=1500000;
    plan.video_segments.segments.push_back({
        "https://r1.googlevideo.com/videoplayback",millis});
    plan.video_segments.total_duration_ms=millis;
    ttnx::switch_app::SwitchHlsCacheSink sink;
    if(!sink.begin(plan))return 7;
    if(!sink.write_segment(false,false,0,segment))return 8;
    if(!append_audio(sink,argv[2]))return 9;
    if(!sink.publish(plan))return 10;
    return 0;
}
