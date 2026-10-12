#include "tizentube_nx/playback/hls_manifest.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttnx::playback {
namespace {
constexpr std::size_t kMaxManifest=1024*1024;
constexpr std::size_t kMaxLine=8192;
constexpr std::size_t kMaxSegments=4096;
constexpr std::uint64_t kMaxDurationMs=7ULL*24*3600*1000;
bool number(std::string_view s,std::uint64_t& value) {
    if(s.empty() || s.size()>20)return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),value);
    return r.ec==std::errc{} && r.ptr==s.data()+s.size();
}
bool decimal_millis(std::string_view input,std::uint64_t& result) {
    const auto dot=input.find('.');
    auto whole=input.substr(0,dot);
    std::uint64_t integer=0;
    if(!number(whole,integer) || integer>3600)return false;
    result=integer*1000;
    if(dot==std::string_view::npos)return true;
    auto fraction=input.substr(dot+1);
    if(fraction.empty() || fraction.size()>6)return false;
    std::uint64_t extra=0;
    for(std::size_t i=0;i<fraction.size();++i) {
        if(fraction[i]<'0'||fraction[i]>'9')return false;
        if(i<3)extra=extra*10+static_cast<unsigned>(fraction[i]-'0');
    }
    for(std::size_t i=std::min<std::size_t>(3,fraction.size());i<3;++i)extra*=10;
    result+=extra;
    return true;
}
std::string trim(std::string_view x) {
    while(!x.empty() && (x.front()==' ' || x.front()=='\t'))x.remove_prefix(1);
    while(!x.empty() && (x.back()==' ' || x.back()=='\t' || x.back()=='\r'))x.remove_suffix(1);
    return std::string(x);
}
bool ascii_line(std::string_view s) {
    if(s.size()>kMaxLine)return false;
    for(unsigned char c:s)if((c<32 && c!='\t')||c>126)return false;
    return true;
}
std::optional<std::vector<std::string>> split_lines(std::string_view body) {
    if(body.size()<7 || body.size()>kMaxManifest || body.find('\0')!=std::string_view::npos)
        return std::nullopt;
    std::vector<std::string> lines;
    while(!body.empty()) {
        const auto at=body.find('\n');
        const auto line=body.substr(0,at);
        if(!ascii_line(line) || lines.size()>=kMaxSegments*4)return std::nullopt;
        lines.push_back(trim(line));
        if(at==std::string_view::npos)break;
        body.remove_prefix(at+1);
    }
    if(lines.empty() || lines.front()!="#EXTM3U")return std::nullopt;
    return lines;
}
bool reviewed_dns_label(std::string_view name) {
    if(name.empty() || name.size()>63 || name.front()=='-' || name.back()=='-')return false;
    for(unsigned char c:name)
        if(!(c>='a'&&c<='z') && !(c>='0'&&c<='9') && c!='-')return false;
    return true;
}
bool reviewed_host(std::string_view hostname) {
    constexpr std::string_view suffix=".googlevideo.com";
    if(!hostname.ends_with(suffix) || hostname.size()<=suffix.size()+1)return false;
    hostname.remove_suffix(suffix.size());
    while(!hostname.empty()) {
        const auto dot=hostname.find('.');
        if(!reviewed_dns_label(hostname.substr(0,dot)))return false;
        if(dot==std::string_view::npos)break;
        hostname.remove_prefix(dot+1);
    }
    return true;
}
bool safe_child(std::string_view url) {
    if(url.size()<35 || url.size()>8192 || !url.starts_with("https://"))
        return false;
    for(unsigned char c:url)if(c<33 || c>126)return false;
    const auto host_start=std::size_t{8};
    const auto slash=url.find('/',host_start);
    if(slash==std::string_view::npos)return false;
    const auto host=url.substr(host_start,slash-host_start);
    if(host.find('@')!=std::string_view::npos ||
       host.find(':')!=std::string_view::npos ||
       host.find('%')!=std::string_view::npos ||
       !reviewed_host(host))return false;
    auto target=url.substr(slash);
    if(target.find('#')!=std::string_view::npos ||
       target.find('\\')!=std::string_view::npos ||
       target.find("//")!=std::string_view::npos)return false;
    const auto query=target.find('?');
    target=target.substr(0,query);
    if(target.find("/../")!=std::string_view::npos ||
       target.find("/./")!=std::string_view::npos)return false;
    std::string path(target);
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if(path.find("%2e")!=std::string::npos || path.find("%2f")!=std::string::npos ||
       path.find("%5c")!=std::string::npos)return false;
    return path.starts_with("/api/manifest/hls_") ||
           path.starts_with("/videoplayback");
}
std::optional<std::string> resolve_url(std::string_view base,std::string_view item) {
    if(item.empty() || item.size()>8192 ||
       item.front()=='#' || item.starts_with("//"))return std::nullopt;
    std::string uri;
    if(item.starts_with("https://")) {
        uri=std::string(item);
    } else {
        if(item.find("://")!=std::string_view::npos ||
           item.find('\\')!=std::string_view::npos)return std::nullopt;
        const auto origin_end=base.find('/',8);
        if(origin_end==std::string_view::npos)return std::nullopt;
        const auto origin=base.substr(0,origin_end);
        if(item.front()=='/')uri=std::string(origin)+std::string(item);
        else {
            auto path=base.substr(0,base.find('?'));
            const auto slash=path.rfind('/');
            if(slash==std::string_view::npos)return std::nullopt;
            uri=std::string(path.substr(0,slash+1))+std::string(item);
        }
    }
    if(!safe_child(uri))return std::nullopt;
    return uri;
}
using Attributes=std::map<std::string,std::string>;
std::optional<Attributes> parse_attributes(std::string_view input) {
    if(input.size()>kMaxLine)return std::nullopt;
    Attributes attrs;
    while(!input.empty()) {
        const auto eq=input.find('=');
        if(eq==std::string_view::npos || eq==0 || eq>64)return std::nullopt;
        auto key=std::string(input.substr(0,eq));
        for(unsigned char c:key)if(!std::isalnum(c) && c!='-')return std::nullopt;
        input.remove_prefix(eq+1);
        std::string value;
        if(!input.empty() && input.front()=='"') {
            input.remove_prefix(1);
            const auto close=input.find('"');
            if(close==std::string_view::npos)return std::nullopt;
            value=std::string(input.substr(0,close));
            input.remove_prefix(close+1);
        } else {
            const auto comma=input.find(',');
            value=std::string(input.substr(0,comma));
            input.remove_prefix(comma==std::string_view::npos?input.size():comma);
        }
        if(value.empty() || attrs.find(key)!=attrs.end())return std::nullopt;
        attrs.emplace(std::move(key),std::move(value));
        if(input.empty())break;
        if(input.front()!=',')return std::nullopt;
        input.remove_prefix(1);
    }
    return attrs;
}
bool resolution(std::string_view s,unsigned& w,unsigned& h) {
    const auto x=s.find('x');
    if(x==std::string_view::npos)return false;
    std::uint64_t width=0,height=0;
    if(!number(s.substr(0,x),width) || !number(s.substr(x+1),height) ||
       width==0 || width>4096 || height==0 || height>2160)return false;
    w=static_cast<unsigned>(width);h=static_cast<unsigned>(height);
    return true;
}
bool codecs_supported(std::string_view codecs) {
    return (codecs.find("avc1.")!=std::string_view::npos ||
            codecs.find("avc3.")!=std::string_view::npos) &&
           codecs.find("mp4a.40.")!=std::string_view::npos &&
           codecs.find("av01.")==std::string_view::npos &&
           codecs.find("hvc1.")==std::string_view::npos &&
           codecs.find("hev1.")==std::string_view::npos;
}
bool dangerous_tag(std::string_view tag) {
    return tag.starts_with("#EXT-X-KEY:") ||
           tag.starts_with("#EXT-X-SESSION-KEY:") ||
           tag.starts_with("#EXT-X-CUE-OUT") ||
           tag.starts_with("#EXT-X-CUE-IN") ||
           tag.starts_with("#EXT-X-DATERANGE:") ||
           tag.starts_with("#EXT-X-DISCONTINUITY") ||
           tag.starts_with("#EXT-X-BYTERANGE:") ||
           tag.starts_with("#EXT-X-I-FRAME-STREAM-INF:");
}
} // namespace

bool is_reviewable_hls_child_url(std::string_view url) {
    return safe_child(url);
}

std::optional<HlsMaster> parse_hls_master_playlist(
    std::string_view body, std::string_view master_url) {
    if(!safe_child(master_url))return std::nullopt;
    const auto lines=split_lines(body);
    if(!lines)return std::nullopt;
    std::map<std::string,std::string> audio_groups;
    // Process media-group declarations first; stream entries may come earlier.
    for(const auto& line:*lines) {
        if(dangerous_tag(line))return std::nullopt;
        constexpr std::string_view tag="#EXT-X-MEDIA:";
        if(!std::string_view(line).starts_with(tag))continue;
        const auto attrs=parse_attributes(std::string_view(line).substr(tag.size()));
        if(!attrs)return std::nullopt;
        auto type=attrs->find("TYPE");
        if(type==attrs->end() || type->second!="AUDIO")continue;
        auto group=attrs->find("GROUP-ID"), uri=attrs->find("URI");
        if(group==attrs->end() || uri==attrs->end())continue;
        const auto checked=resolve_url(master_url,uri->second);
        if(!checked || !audio_groups.emplace(group->second,*checked).second)
            return std::nullopt;
        if(audio_groups.size()>32)return std::nullopt;
    }
    HlsMaster result;
    constexpr std::string_view stream_tag="#EXT-X-STREAM-INF:";
    for(std::size_t i=1;i<lines->size();++i) {
        const auto& line=(*lines)[i];
        if(!std::string_view(line).starts_with(stream_tag))continue;
        if(i+1>=lines->size())return std::nullopt;
        const auto attrs=parse_attributes(std::string_view(line).substr(stream_tag.size()));
        const auto& next=(*lines)[++i];
        if(!attrs || next.empty() || next.front()=='#')return std::nullopt;
        const auto uri=resolve_url(master_url,next);
        if(!uri)return std::nullopt;
        auto codec=attrs->find("CODECS"), resolution_attr=attrs->find("RESOLUTION");
        if(codec==attrs->end() || resolution_attr==attrs->end() ||
           !codecs_supported(codec->second))continue;
        HlsVariant candidate;
        candidate.playlist_url=*uri;
        if(!resolution(resolution_attr->second,candidate.width,candidate.height))continue;
        candidate.fps=30; // If unspecified, bound decoder to 30fps for first M3 gate.
        if(auto frame=attrs->find("FRAME-RATE");frame!=attrs->end()) {
            std::uint64_t fps_thousand=0;
            if(!decimal_millis(frame->second,fps_thousand) ||
               fps_thousand<1000 || fps_thousand>60000)continue;
            candidate.fps=static_cast<unsigned>((fps_thousand+999)/1000);
        }
        if(auto bandwidth=attrs->find("BANDWIDTH");bandwidth!=attrs->end()) {
            if(!number(bandwidth->second,candidate.bandwidth) ||
               candidate.bandwidth==0 || candidate.bandwidth>100000000)continue;
        }
        if(auto audio=attrs->find("AUDIO");audio!=attrs->end()) {
            auto group=audio_groups.find(audio->second);
            if(group==audio_groups.end())continue;
            candidate.audio_playlist_url=group->second;
        }
        if(result.compatible.size()>=64)return std::nullopt;
        result.compatible.push_back(std::move(candidate));
    }
    if(result.compatible.empty())return std::nullopt;
    return result;
}

std::optional<HlsVariant> choose_hls_variant(
    const HlsMaster& m,unsigned max_height,unsigned max_fps) {
    std::optional<HlsVariant> best;
    for(const auto& item:m.compatible) {
        if(!item.width || !item.height || item.height>max_height ||
           !item.fps || item.fps>max_fps ||
           !safe_child(item.playlist_url) ||
           (!item.audio_playlist_url.empty()&&!safe_child(item.audio_playlist_url)))
            continue;
        if(!best || std::pair(item.height,item.bandwidth)>
                     std::pair(best->height,best->bandwidth))best=item;
    }
    return best;
}

std::optional<HlsVariant> choose_cache_bounded_hls_variant(
    const HlsMaster& playlist, std::uint64_t duration_ms,
    std::uint64_t max_cache_bytes, unsigned max_height,
    unsigned max_fps) {
    if(!duration_ms || duration_ms>7ULL*24*3600*1000 ||
       !max_cache_bytes)return std::nullopt;
    const auto safe_budget=max_cache_bytes-max_cache_bytes/5;
    std::optional<HlsVariant> best;
    for(const auto& item:playlist.compatible) {
        if(!item.height || item.height>max_height || !item.fps ||
           item.fps>max_fps || !safe_child(item.playlist_url) ||
           (!item.audio_playlist_url.empty() &&
            !safe_child(item.audio_playlist_url)))continue;
        if(item.bandwidth==0 && duration_ms>120000)continue;
        if(item.bandwidth) {
            // Reviewed manifest caps bitrate at 100Mbit/s; duration is
            // bounded above. Their product fits a uint64_t.
            if(item.bandwidth>100000000)continue;
            const auto projected=(item.bandwidth*duration_ms+7999)/8000;
            if(projected>safe_budget)continue;
        }
        if(!best || std::pair(item.height,item.bandwidth)>
                     std::pair(best->height,best->bandwidth))
            best=item;
    }
    return best;
}

std::optional<HlsMediaPlaylist> parse_hls_media_playlist(
    std::string_view body, std::string_view source_url) {
    if(!safe_child(source_url))return std::nullopt;
    const auto lines=split_lines(body);
    if(!lines)return std::nullopt;
    HlsMediaPlaylist playlist;
    bool endlist=false,have_duration=false;
    std::uint64_t duration_ms=0;
    for(std::size_t i=1;i<lines->size();++i) {
        const auto& line=(*lines)[i];
        if(line.empty())continue;
        if(line.front()=='#') {
            if(dangerous_tag(line))return std::nullopt;
            if(line=="#EXT-X-ENDLIST"){endlist=true;continue;}
            if(std::string_view(line).starts_with("#EXT-X-STREAM-INF:"))
                return std::nullopt; // nested master unsupported
            if(std::string_view(line).starts_with("#EXTINF:")) {
                if(have_duration)return std::nullopt;
                const auto value=std::string_view(line).substr(8);
                const auto comma=value.find(',');
                if(!decimal_millis(value.substr(0,comma),duration_ms) ||
                   duration_ms==0 || duration_ms>60000)return std::nullopt;
                have_duration=true;
            }
            if(std::string_view(line).starts_with("#EXT-X-MAP:")) {
                if(playlist.init_segment_url)return std::nullopt;
                const auto attrs=parse_attributes(std::string_view(line).substr(11));
                if(!attrs || attrs->find("BYTERANGE")!=attrs->end())return std::nullopt;
                const auto it=attrs->find("URI");
                if(it==attrs->end())return std::nullopt;
                playlist.init_segment_url=resolve_url(source_url,it->second);
                if(!playlist.init_segment_url)return std::nullopt;
            }
            continue;
        }
        if(endlist || !have_duration || playlist.segments.size()>=kMaxSegments)
            return std::nullopt;
        const auto uri=resolve_url(source_url,line);
        if(!uri || playlist.total_duration_ms>kMaxDurationMs-duration_ms)
            return std::nullopt;
        playlist.segments.push_back({*uri,duration_ms});
        playlist.total_duration_ms+=duration_ms;
        have_duration=false;
    }
    if(!endlist || have_duration || playlist.segments.empty())return std::nullopt;
    return playlist;
}
} // namespace ttnx::playback
