#pragma once
#include "tizentube_nx/playback/hls_manifest.hpp"
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace ttnx::playback {
// Safe local HLS EVENT snapshot. No bearer URL from HlsMediaPlaylist may be
// emitted: all media names are deterministic app-owned relative filenames.
// Caller must atomically replace the playlist file after writing each segment.
// This generator does NOT make a staged cache safe to open; segment lifetime,
// directory stability and libmpv refresh are separate acceptance requirements.
inline std::optional<std::string> make_hls_event_snapshot(
    const HlsMediaPlaylist& playlist,std::size_t persisted_segments,
    bool final_snapshot,bool ts_segments=true,bool audio=false) {
    if(playlist.segments.empty() || playlist.segments.size()>4096 ||
       !persisted_segments || persisted_segments>playlist.segments.size() ||
       (final_snapshot && persisted_segments!=playlist.segments.size()) ||
       (ts_segments && playlist.init_segment_url))return std::nullopt;

    std::uint64_t target_ms=0;
    for(const auto& s:playlist.segments) {
        if(!s.duration_ms || s.duration_ms>60000)return std::nullopt;
        target_ms=std::max(target_ms,s.duration_ms);
    }
    const auto target=(target_ms+999)/1000;
    if(target==0 || target>60)return std::nullopt;
    std::string output="#EXTM3U\n#EXT-X-VERSION:7\n";
    // EVENT preserves the initial media sequence while later segments are
    // appended. Never emit PLAYLIST-TYPE:VOD before ENDLIST is available.
    output+="#EXT-X-PLAYLIST-TYPE:EVENT\n"
            "#EXT-X-MEDIA-SEQUENCE:0\n"
            "#EXT-X-TARGETDURATION:"+std::to_string(target)+"\n";
    if(playlist.init_segment_url)
        output+="#EXT-X-MAP:URI=\""+
            std::string(audio?"a_init.m4s":"v_init.m4s")+"\"\n";
    for(std::size_t i=0;i<persisted_segments;++i) {
        const auto ms=playlist.segments[i].duration_ms;
        char duration[32]{},filename[32]{};
        std::snprintf(duration,sizeof(duration),"%llu.%03llu",
                      static_cast<unsigned long long>(ms/1000),
                      static_cast<unsigned long long>(ms%1000));
        std::snprintf(filename,sizeof(filename),"%c_%05u.%s",
                      audio?'a':'v',static_cast<unsigned>(i),
                      ts_segments?"ts":"m4s");
        output+="#EXTINF:"+std::string(duration)+",\n"+
                filename+"\n";
    }
    if(final_snapshot)output+="#EXT-X-ENDLIST\n";
    if(output.size()>1024*1024)return std::nullopt;
    return output;
}
} // namespace ttnx::playback
