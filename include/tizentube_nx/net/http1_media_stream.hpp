#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

namespace ttnx::net {
// Dedicated HTTP/1.1 *media* stream parser. Never used for M2 browsing.
// Reject framing ambiguity and unknown-length responses instead of buffering.
class Http1MediaStream final {
public:
    static constexpr std::size_t kMaxHeaders=16384;
    static constexpr std::size_t kMaxBody=16*1024*1024;
    static constexpr std::size_t kReceiveChunk=32*1024;
    using Emit=std::function<bool(std::string_view)>;
    enum class State { Headers, Body, Complete, Rejected };
    enum class Error { None, Headers, Status, Framing, Mime, Length, Consumer };
    [[nodiscard]] bool feed(std::string_view input,const Emit& emit) {
        if(state_==State::Rejected || state_==State::Complete || !emit ||
           input.empty() || input.size()>kReceiveChunk)
            return reject(Error::Framing);
        if(state_==State::Headers) {
            const auto end=input.find("\r\n\r\n");
            if(end==std::string_view::npos) {
                if(headers_.size()+input.size()>kMaxHeaders+kReceiveChunk)
                    return reject(Error::Headers);
                headers_.append(input);
                const auto delim=headers_.find("\r\n\r\n");
                if(delim==std::string::npos)
                    return headers_.size()<=kMaxHeaders?true:reject(Error::Headers);
                if(delim+4>kMaxHeaders)return reject(Error::Headers);
                const std::string extra=headers_.substr(delim+4);
                headers_.resize(delim+4);
                if(!parse_headers())return false;
                return extra.empty()?true:push_body(extra,emit);
            }
            if(headers_.size()+end+4>kMaxHeaders)return reject(Error::Headers);
            headers_.append(input.substr(0,end+4));
            if(!parse_headers())return false;
            return end+4==input.size()?true:push_body(input.substr(end+4),emit);
        }
        return push_body(input,emit);
    }
    [[nodiscard]] bool finish() {
        if(state_==State::Rejected)return false;
        if(state_!=State::Body || consumed_!=length_)
            return reject(Error::Length);
        state_=State::Complete;return true;
    }
    [[nodiscard]] State state()const noexcept{return state_;}
    [[nodiscard]] Error error()const noexcept{return error_;}
    [[nodiscard]] std::size_t received()const noexcept{return consumed_;}
    [[nodiscard]] std::size_t expected()const noexcept{return length_;}
    [[nodiscard]] std::size_t buffered_headers()const noexcept{return headers_.size();}
private:
    static bool ci_equal(std::string_view a,std::string_view b) noexcept {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i) {
            char x=a[i],y=b[i];
            if(x>='A'&&x<='Z')x=static_cast<char>(x+32);
            if(y>='A'&&y<='Z')y=static_cast<char>(y+32);
            if(x!=y)return false;
        }
        return true;
    }
    static std::string_view trim(std::string_view s) noexcept {
        while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);
        while(!s.empty()&&(s.back()==' '||s.back()=='\t'))s.remove_suffix(1);
        return s;
    }
    [[nodiscard]] bool reject(Error e) noexcept {
        state_=State::Rejected;error_=e;return false;
    }
    [[nodiscard]] bool parse_headers() {
        const std::string_view all=headers_;
        const auto status_end=all.find("\r\n");
        if(status_end==std::string_view::npos || status_end>128 ||
           all.substr(0,12)!="HTTP/1.1 200" ||
           (status_end>12 && all[12]!=' '))return reject(Error::Status);
        bool have_length=false,have_mime=false,have_encoding=false;
        auto at=status_end+2;
        const auto final=all.size()-2;
        while(at<final) {
            const auto end=all.find("\r\n",at);
            if(end==std::string_view::npos||end>=all.size()||end==at)
                return reject(Error::Headers);
            const auto line=all.substr(at,end-at);
            const auto colon=line.find(':');
            if(colon==std::string_view::npos||colon==0)return reject(Error::Headers);
            for(unsigned char c:line)
                if(c<0x20 && c!='\t')return reject(Error::Headers);
            auto name=line.substr(0,colon);
            auto value=trim(line.substr(colon+1));
            if(ci_equal(name,"transfer-encoding")||ci_equal(name,"location")||
               ci_equal(name,"content-range"))return reject(Error::Framing);
            if(ci_equal(name,"content-length")) {
                if(have_length || value.empty() || value.size()>8 ||
                   (value.size()>1 && value.front()=='0'))return reject(Error::Length);
                have_length=true;
                std::size_t n=0;
                for(char c:value) {
                    if(c<'0'||c>'9')return reject(Error::Length);
                    n=n*10+static_cast<unsigned>(c-'0');
                    if(n>kMaxBody)return reject(Error::Length);
                }
                length_=n;
            } else if(ci_equal(name,"content-type")) {
                if(have_mime)return reject(Error::Mime);
                have_mime=true;
                const auto semi=value.find(';');
                const auto media=trim(value.substr(0,semi));
                if(!ci_equal(media,"video/mp2t") &&
                   !ci_equal(media,"application/octet-stream"))return reject(Error::Mime);
            } else if(ci_equal(name,"content-encoding")) {
                if(have_encoding || !ci_equal(value,"identity"))
                    return reject(Error::Framing);
                have_encoding=true;
            }
            at=end+2;
        }
        if(!have_length||!have_mime||length_<376)return reject(Error::Headers);
        headers_.clear();
        state_=State::Body;return true;
    }
    [[nodiscard]] bool push_body(std::string_view data,const Emit& emit) {
        if(data.size()>length_-consumed_)return reject(Error::Length);
        if(!data.empty() && !emit(data))return reject(Error::Consumer);
        consumed_+=data.size();return true;
    }
    State state_{State::Headers};Error error_{Error::None};
    std::string headers_{};
    std::size_t length_{0},consumed_{0};
};
} // namespace ttnx::net
