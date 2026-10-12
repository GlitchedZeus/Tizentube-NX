#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
namespace ttnx::net {
// Response framing extracted for a host-testable libnx SSL read decision.
// Unknown transfer encodings retain the existing EOF parsing path, while
// unambiguous Content-Length responses terminate at their exact byte count.
enum class HttpFrameKind { HeadersPending, NeedEof, FixedLength, Rejected };
struct HttpFrame {HttpFrameKind kind;std::size_t total{0};};
inline bool http_frame_ieq(std::string_view lhs,std::string_view rhs) noexcept {
    if(lhs.size()!=rhs.size())return false;
    for(std::size_t i=0;i<lhs.size();++i) {
        auto a=lhs[i],b=rhs[i];
        if(a>='A'&&a<='Z')a=static_cast<char>(a+32);
        if(b>='A'&&b<='Z')b=static_cast<char>(b+32);
        if(a!=b)return false;
    }
    return true;
}
inline HttpFrame inspect_http1_frame(
    std::string_view bytes,std::size_t raw_cap) noexcept {
    const auto at=bytes.find("\r\n\r\n");
    if(at==std::string_view::npos)
        return {bytes.size()<=65536?HttpFrameKind::HeadersPending:
                                       HttpFrameKind::Rejected,0};
    if(at>65536 || at+4>raw_cap)return {HttpFrameKind::Rejected,0};
    const auto status_end=bytes.find("\r\n");
    if(status_end==std::string_view::npos || status_end>at)
        return {HttpFrameKind::Rejected,0};
    bool have_length=false,transfer_encoding=false;
    std::size_t length=0;
    auto from=status_end+2;
    while(from<at) {
        const auto end=bytes.find("\r\n",from);
        if(end==std::string_view::npos || end>at)return {HttpFrameKind::Rejected,0};
        const auto line=bytes.substr(from,end-from);
        const auto colon=line.find(':');
        if(colon==std::string_view::npos || colon==0)return {HttpFrameKind::Rejected,0};
        const auto key=line.substr(0,colon);
        auto value=line.substr(colon+1);
        while(!value.empty() && (value.front()==' ' || value.front()=='\t'))
            value.remove_prefix(1);
        while(!value.empty() && (value.back()==' ' || value.back()=='\t'))
            value.remove_suffix(1);
        if(http_frame_ieq(key,"transfer-encoding")) {
            if(transfer_encoding)return {HttpFrameKind::Rejected,0};
            transfer_encoding=true;
        }
        if(http_frame_ieq(key,"content-length")) {
            if(have_length || value.empty())return {HttpFrameKind::Rejected,0};
            have_length=true;
            for(char digit:value) {
                if(digit<'0'||digit>'9')return {HttpFrameKind::Rejected,0};
                auto n=static_cast<std::size_t>(digit-'0');
                if(length>(raw_cap-n)/10)return {HttpFrameKind::Rejected,0};
                length=length*10+n;
            }
        }
        from=end+2;
    }
    // TE+CL is a dangerous ambiguous framing combination, fail closed.
    if(transfer_encoding && have_length)return {HttpFrameKind::Rejected,0};
    if(transfer_encoding || !have_length)return {HttpFrameKind::NeedEof,0};
    if(length>raw_cap-(at+4))return {HttpFrameKind::Rejected,0};
    return {HttpFrameKind::FixedLength,at+4+length};
}
} // namespace ttnx::net
