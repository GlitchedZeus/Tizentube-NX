#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace ttnx::playback {
// Experimental hardware-lab only: present validated, fixed-name local video
// and AAC as one mpv EDL timeline rather than separate --audio-files tracks.
// The inline EDL creates no persistent files and never refers to a CDN.
[[nodiscard]] inline std::optional<std::string> local_adaptive_edl(
    std::string_view video,std::string_view audio) {
    auto safe=[](std::string_view s,const char* ending)->bool {
        constexpr std::string_view base="media-cache/";
        if(s.size()>256||s.find("://")!=std::string_view::npos||
           s.find('\n')!=std::string_view::npos||
           s.find('\r')!=std::string_view::npos||
           s.find(';')!=std::string_view::npos)return false;
        if(!(s.starts_with("sdmc:/switch/TizenTube-NX/") ||
             s.starts_with("/tmp/ttnx-adaptive-cache-test/switch/TizenTube-NX/")))
            return false;
        const auto pos=s.find(base);
        return pos!=std::string_view::npos &&
               s.substr(pos+base.size())==ending;
    };
    if(!safe(video,"adaptive_video.mp4")||
       !safe(audio,"adaptive_audio.m4a"))return std::nullopt;
    return std::string("edl://%")+std::to_string(video.size())+"%"+
        std::string(video)+";!new_stream;%"+std::to_string(audio.size())+
        "%"+std::string(audio);
}
} // namespace ttnx::playback
