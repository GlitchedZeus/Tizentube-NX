#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ttnx::playback {
// A strict pre-open gate for the M4 opt-in mpv local-file route. It is NOT
// evidence of a successful H264/AAC decode. Codec verification and durable
// media publication belong to the worker and player respectively.
// Accept only our own sliding muxed-MPEG-TS playlists, not arbitrary HLS.
struct LocalLiveHlsGate {
    std::size_t segments{0};
    std::uint64_t first_sequence{0};
    std::uint64_t buffered_ms{0};
    bool complete{false};
    [[nodiscard]] bool playable_prefix() const noexcept {
        return segments>0 && buffered_ms>=3000;
    }
};

[[nodiscard]] inline bool parse_local_live_hls(
    std::string_view source, LocalLiveHlsGate& result) noexcept {
    result={};
    if(source.empty() || source.size()>65536 || source.back()!='\n')return false;
    // The sink only writes ASCII/LF and no URI escapes. Fail closed on bytes
    // that could be interpreted differently by the libavformat parser.
    for(const unsigned char c:source)
        if(c==0 || c=='\r' || c>=127 || c<9 || (c>13 && c<32))return false;
    std::size_t start=0;
    auto next=[&](std::string_view& line) noexcept {
        if(start>=source.size())return false;
        const auto end=source.find('\n',start);
        if(end==std::string_view::npos)return false;
        line=source.substr(start,end-start);
        start=end+1;
        return true;
    };
    std::string_view line;
    if(!next(line)||line!="#EXTM3U" ||
       !next(line)||line!="#EXT-X-VERSION:3" ||
       !next(line)||!line.starts_with("#EXT-X-TARGETDURATION:"))return false;
    auto number=[](std::string_view text,std::uint64_t& v) noexcept {
        if(text.empty() || text.size()>12 || text.front()=='+' ||
           text.front()=='-' || (text.size()>1 && text.front()=='0'))return false;
        auto e=std::from_chars(text.data(),text.data()+text.size(),v);
        return e.ec==std::errc{} && e.ptr==text.data()+text.size();
    };
    std::uint64_t target=0;
    if(!number(line.substr(sizeof("#EXT-X-TARGETDURATION:")-1),target) ||
       target==0 || target>60 || !next(line) ||
       !line.starts_with("#EXT-X-MEDIA-SEQUENCE:") ||
       !number(line.substr(sizeof("#EXT-X-MEDIA-SEQUENCE:")-1),
               result.first_sequence) || result.first_sequence>=4096)return false;
    std::uint64_t sequence=result.first_sequence;
    while(next(line)) {
        if(line=="#EXT-X-ENDLIST") {
            if(start!=source.size() || result.segments==0)return false;
            result.complete=true;
            return true;
        }
        if(!line.starts_with("#EXTINF:") || !line.ends_with(","))return false;
        line.remove_prefix(8);
        line.remove_suffix(1);
        // Sink's canonical duration is seconds.milliseconds (three digits),
        // so no NaN, exponent, tags or URI parsing ambiguity is permitted.
        const auto period=line.find('.');
        if(period==std::string_view::npos || period==0 ||
           line.size()!=period+4)return false;
        std::uint64_t seconds=0,millis=0;
        if(!number(line.substr(0,period),seconds))return false;
        for(const auto ch : line.substr(period+1)) {
            if(ch<'0' || ch>'9')return false;
            millis=millis*10+static_cast<std::uint64_t>(ch-'0');
        }
        if(millis>999 || (seconds==0 && millis==0) || seconds>60 ||
           (seconds==60 && millis!=0))return false;
        if(!next(line)||line.size()!=10 ||
           line.substr(0,2)!="v_" || line.substr(7)!=".ts" ||
           sequence>=4096)return false;
        std::uint64_t encoded=0;
        for(std::size_t i=2;i<7;++i) {
            if(line[i]<'0'||line[i]>'9')return false;
            encoded=encoded*10+static_cast<std::uint64_t>(line[i]-'0');
        }
        if(encoded!=sequence)return false;
        if(result.buffered_ms>UINT64_MAX-(seconds*1000+millis))return false;
        result.buffered_ms+=seconds*1000+millis;
        ++sequence;
        ++result.segments;
        if(result.segments>192)return false; // bounded local demux window
    }
    return result.segments>0;
}
} // namespace ttnx::playback
