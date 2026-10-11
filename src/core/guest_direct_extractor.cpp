#include "tizentube_nx/playback/guest_direct_extractor.hpp"
#include "tizentube_nx/playback/vision_hls.hpp"
#include "tizentube_nx/playback/source_routes.hpp"

#include "tizentube_nx/core/url.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace ttnx::playback {
namespace {

// Bounded streaming JSON reader. Unknown Youtube metadata is recursively
// skipped without ever materializing ads, tracking, titles or thumbnails.
// A generic entire-response DOM would waste memory on Switch.
class Json {
public:
    explicit Json(std::string_view s) : data_(s) {}
    void ws() {
        while (pos_ < data_.size() &&
               (data_[pos_] == ' ' || data_[pos_] == '\t' ||
                data_[pos_] == '\r' || data_[pos_] == '\n')) ++pos_;
    }
    bool take(char c) {
        ws();
        if (pos_ >= data_.size() || data_[pos_] != c) return false;
        ++pos_;
        return true;
    }
    bool done() { ws(); return pos_ == data_.size(); }

    bool text(std::string* out = nullptr, std::size_t limit = 8192) {
        if (!take('"')) return false;
        if (out) out->clear();
        while (pos_ < data_.size()) {
            const unsigned char ch = static_cast<unsigned char>(data_[pos_++]);
            if (ch == '"') return true;
            if (ch < 32) return false;
            unsigned code = ch;
            if (ch == '\\') {
                if (pos_ >= data_.size()) return false;
                const char e = data_[pos_++];
                switch(e) {
                    case '"': code = '"'; break;
                    case '\\': code = '\\'; break;
                    case '/': code = '/'; break;
                    case 'b': code = '\b'; break;
                    case 'f': code = '\f'; break;
                    case 'n': code = '\n'; break;
                    case 'r': code = '\r'; break;
                    case 't': code = '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > data_.size()) return false;
                        code = 0;
                        for (int j=0; j<4; ++j) {
                            const char h = data_[pos_++];
                            if (h >= '0' && h <= '9') code = code * 16 + (h-'0');
                            else if (h >= 'a' && h <= 'f') code = code * 16 + (h-'a'+10);
                            else if (h >= 'A' && h <= 'F') code = code * 16 + (h-'A'+10);
                            else return false;
                        }
                        break;
                    }
                    default: return false;
                }
            }
            // Metadata keys, codec hints and selected stream URLs must be
            // strict ASCII. Unknown Unicode strings are skipped without copy.
            if (out) {
                if (code > 127 || out->size() >= limit) return false;
                out->push_back(static_cast<char>(code));
            }
        }
        return false;
    }

    bool boolean(bool& out) {
        ws();
        if (data_.substr(pos_,4) == "true") {
            pos_ += 4; out = true; return true;
        }
        if (data_.substr(pos_,5) == "false") {
            pos_ += 5; out = false; return true;
        }
        return false;
    }

    bool number(std::uint64_t* output=nullptr) {
        ws();
        if (pos_ >= data_.size()) return false;
        const auto start=pos_;
        bool negative = false;
        if (data_[pos_] == '-') { negative=true; ++pos_; }
        if (pos_ >= data_.size()) return false;
        if (data_[pos_] == '0') ++pos_;
        else {
            if (data_[pos_] < '1' || data_[pos_] > '9') return false;
            while (pos_ < data_.size() &&
                   data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
        }
        bool whole = true;
        if (pos_ < data_.size() && data_[pos_] == '.') {
            whole=false; ++pos_;
            const auto digits=pos_;
            while (pos_ < data_.size() && data_[pos_] >= '0' &&
                   data_[pos_] <= '9') ++pos_;
            if (pos_==digits) return false;
        }
        if (pos_ < data_.size() && (data_[pos_] == 'e' || data_[pos_] == 'E')) {
            whole=false; ++pos_;
            if (pos_ < data_.size() &&
                (data_[pos_] == '+' || data_[pos_] == '-')) ++pos_;
            const auto digits=pos_;
            while (pos_ < data_.size() && data_[pos_] >= '0' &&
                   data_[pos_] <= '9') ++pos_;
            if (pos_==digits) return false;
        }
        if (!output) return true;
        if (!whole || negative) return false;
        std::uint64_t value=0;
        for (auto i=start; i<pos_; ++i) {
            const unsigned digit = data_[i]-'0';
            if (value > (std::numeric_limits<std::uint64_t>::max()-digit)/10)
                return false;
            value=value*10+digit;
        }
        *output=value;
        return true;
    }

    template<class F> bool object(F field, unsigned depth=0) {
        if (depth > 24 || !take('{')) return false;
        if (take('}')) return true;
        for (unsigned fields=0; fields<4096; ++fields) {
            std::string key;
            if (!text(&key,64) || !take(':') || !field(key,depth+1)) return false;
            if (take('}')) return true;
            if (!take(',')) return false;
        }
        return false;
    }
    template<class F> bool array(F element, unsigned depth=0) {
        if (depth > 24 || !take('[')) return false;
        if (take(']')) return true;
        for (unsigned items=0; items<4096; ++items) {
            if (!element(depth+1)) return false;
            if (take(']')) return true;
            if (!take(',')) return false;
        }
        return false;
    }
    bool skip(unsigned depth=0) {
        ws();
        if (depth > 24 || pos_ >= data_.size()) return false;
        const char ch=data_[pos_];
        if (ch == '"') return text();
        if (ch == '{') return object([&](std::string_view, unsigned d) {
            return skip(d);
        },depth);
        if (ch == '[') return array([&](unsigned d) {
            return skip(d);
        },depth);
        if (ch == 't' || ch == 'f') { bool x=false;return boolean(x); }
        if (ch == 'n' && data_.substr(pos_,4) == "null") {
            pos_+=4;return true;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return number();
        return false;
    }
private:
    std::string_view data_;
    std::size_t pos_{0};
};

bool only_once(unsigned& bits, unsigned bit) {
    if (bits & bit) return false;
    bits |= bit;
    return true;
}

bool uint_string(std::string_view text, std::uint64_t& out) {
    if (text.empty() || text.size() > 18) return false;
    out=0;
    for (const unsigned char ch : text) {
        if (ch<'0'||ch>'9') return false;
        const unsigned x=ch-'0';
        if (out>(std::numeric_limits<std::uint64_t>::max()-x)/10) return false;
        out=out*10+x;
    }
    return true;
}

bool parse_status(Json& in, std::string& status, unsigned depth) {
    unsigned seen=0;
    return in.object([&](std::string_view key, unsigned d) {
        if (key=="status") return only_once(seen,1) && in.text(&status,32);
        return in.skip(d);
    },depth) && seen==1;
}

struct VideoInfo {
    std::string video_id;
    std::uint64_t duration_sec{0};
    bool short_video{false};
    bool live{false};
    unsigned seen{0};
};
bool parse_details(Json& in, VideoInfo& out, unsigned depth) {
    return in.object([&](std::string_view key, unsigned d) {
        if (key=="videoId")
            return only_once(out.seen,1) && in.text(&out.video_id,11);
        if (key=="lengthSeconds") {
            std::string seconds;
            return only_once(out.seen,2) && in.text(&seconds,18) &&
                   uint_string(seconds,out.duration_sec);
        }
        if (key=="isShort")
            return only_once(out.seen,4) && in.boolean(out.short_video);
        if (key=="isLiveContent")
            return only_once(out.seen,8) && in.boolean(out.live);
        return in.skip(d);
    },depth) && (out.seen & 3)==3;
}

struct RawFormat {
    std::string url;
    std::string mime;
    std::uint64_t bitrate{0};
    unsigned width{0}, height{0}, fps{0};
    bool cipher{false}, drm{false}, color_info{false};
    unsigned seen{0};
};
bool parse_format(Json& in, RawFormat& f, unsigned depth) {
    return in.object([&](std::string_view key, unsigned d) {
        if (key=="url")
            return only_once(f.seen,1) && in.text(&f.url,8192);
        if (key=="mimeType")
            return only_once(f.seen,2) && in.text(&f.mime,512);
        if (key=="bitrate") {
            if (!only_once(f.seen,4)) return false;
            return in.number(&f.bitrate);
        }
        if (key=="width" || key=="height" || key=="fps") {
            std::uint64_t value=0;
            const unsigned bit=key=="width"?8:key=="height"?16:32;
            if (!only_once(f.seen,bit) || !in.number(&value) ||
                value>10000) return false;
            const auto u=static_cast<unsigned>(value);
            if (bit==8) f.width=u;
            else if (bit==16) f.height=u;
            else f.fps=u;
            return true;
        }
        if (key=="signatureCipher" || key=="cipher") {
            f.cipher=true;
            return in.skip(d);
        }
        if (key=="drmFamilies") {
            f.drm=true;
            return in.skip(d);
        }
        if (key=="colorInfo") {
            f.color_info=true; // HDR/10-bit uncertainty: reject for now
            return in.skip(d);
        }
        return in.skip(d);
    },depth);
}

void accept_format(const RawFormat& f, ExtractionResult& data) {
    if (f.cipher || f.drm || f.color_info || f.bitrate==0 ||
        !is_reviewable_googlevideo_https_url(f.url)) return;
    const bool is_video = f.mime.starts_with("video/mp4");
    const bool is_audio = f.mime.starts_with("audio/mp4");
    if (is_video && f.mime.find("avc1.") != std::string::npos &&
        f.width && f.height && f.fps && f.fps<=60 &&
        data.video.size()<200) {
        VideoFormat v;
        v.url=f.url;
        v.codec=VideoCodec::H264;
        v.muxed_audio=f.mime.find("mp4a.")!=std::string::npos
            ? AudioCodec::Aac : AudioCodec::Unknown;
        v.width=f.width;v.height=f.height;v.fps=f.fps;
        v.bit_depth=8;v.bitrate=f.bitrate;
        data.video.push_back(std::move(v));
    } else if (is_audio && f.mime.find("mp4a.")!=std::string::npos &&
               f.bitrate<=1'000'000 && data.audio.size()<200) {
        AudioFormat a;
        a.url=f.url; a.codec=AudioCodec::Aac;
        a.bitrate=f.bitrate;
        data.audio.push_back(std::move(a));
    }
}

bool parse_streams(Json& in, ExtractionResult& data, unsigned depth) {
    unsigned seen=0;
    return in.object([&](std::string_view key, unsigned d) {
        unsigned bit=0;
        if (key=="formats") bit=1;
        else if (key=="adaptiveFormats") bit=2;
        if (!bit) return in.skip(d);
        if (!only_once(seen,bit)) return false;
        unsigned count=0;
        return in.array([&](unsigned arr_depth) {
            if (++count>200) return false;
            RawFormat f;
            if (!parse_format(in,f,arr_depth)) return false;
            accept_format(f,data);
            return true;
        },d);
    },depth) && seen!=0;
}

} // namespace

const char* describe_direct_player_failure(DirectPlayerFailure reason) {
    switch (reason) {
        case DirectPlayerFailure::None: return "Player source ready.";
        case DirectPlayerFailure::InvalidRequest: return "Video request rejected before HTTPS.";
        case DirectPlayerFailure::HttpTransport: return "YouTube player HTTPS transport failed.";
        case DirectPlayerFailure::HttpStatus: return "YouTube player returned non-200 HTTP status.";
        case DirectPlayerFailure::MalformedResponse: return "Player JSON missing required fields or invalid.";
        case DirectPlayerFailure::PlayabilityBlocked: return "YouTube did not permit guest playback.";
        case DirectPlayerFailure::WrongIdentity: return "YouTube returned a different video ID.";
        case DirectPlayerFailure::ShortsOrLive: return "Shorts/live sources blocked by current M3 policy.";
        case DirectPlayerFailure::MissingStreamingData: return "Guest player returned no streamingData.";
        case DirectPlayerFailure::NoSupportedDirectFormats:
            return "No direct H.264/AAC media URLs passed the source checks.";
    }
    return "Unknown player source error.";
}

namespace {
std::optional<ExtractionResult> parse_direct_guest_player_diagnosed(
    std::string_view body, std::string_view expected_video_id,
    DirectPlayerFailure& reason) {
    reason = DirectPlayerFailure::MalformedResponse;
    if (body.empty() || body.size() > 1024 * 1024 ||
        !ttnx::core::is_valid_video_id(expected_video_id)) return std::nullopt;
    Json in(body);
    ExtractionResult data;
    VideoInfo info;
    std::string status;
    unsigned seen=0;
    const bool ok=in.object([&](std::string_view key, unsigned d) {
        if (key=="playabilityStatus")
            return only_once(seen,1) && parse_status(in,status,d);
        if (key=="videoDetails")
            return only_once(seen,2) && parse_details(in,info,d);
        if (key=="streamingData")
            return only_once(seen,4) && parse_streams(in,data,d);
        return in.skip(d);
    });
    if (!ok || !in.done() || !(seen & 1) || !(seen & 2))
        return std::nullopt;
    if (status != "OK") {
        reason=DirectPlayerFailure::PlayabilityBlocked;
        return std::nullopt;
    }
    if (info.video_id != expected_video_id) {
        reason=DirectPlayerFailure::WrongIdentity;
        return std::nullopt;
    }
    if (info.short_video || info.live) {
        reason=DirectPlayerFailure::ShortsOrLive;
        return std::nullopt;
    }
    if (info.duration_sec == 0 || info.duration_sec > 7ULL*24*3600)
        return std::nullopt;
    if (!(seen & 4)) {
        reason=DirectPlayerFailure::MissingStreamingData;
        return std::nullopt;
    }
    if (data.video.empty() && data.audio.empty()) {
        reason=DirectPlayerFailure::NoSupportedDirectFormats;
        return std::nullopt;
    }
    data.video_id=info.video_id;
    data.duration_ms=info.duration_sec*1000;
    data.is_short=false;
    data.is_live=false;
    reason=DirectPlayerFailure::None;
    return data;
}
} // namespace

std::optional<ExtractionResult> parse_direct_guest_player(
    std::string_view body, std::string_view expected_video_id) {
    DirectPlayerFailure reason{};
    return parse_direct_guest_player_diagnosed(body, expected_video_id, reason);
}

std::optional<ExtractionResult> GuestDirectExtractor::resolve(
    std::string_view normal_video_id) {
    last_http_status_=0;
    last_failure_=DirectPlayerFailure::InvalidRequest;
    const auto request=youtube::make_player_request(session_,normal_video_id);
    if (!request) return std::nullopt;
    const auto result=browse_http_.perform(*request);
    if (!result.ok) {
        last_failure_=DirectPlayerFailure::HttpTransport;
        return std::nullopt;
    }
    last_http_status_=result.response.status_code;
    if (last_http_status_!=200) {
        last_failure_=DirectPlayerFailure::HttpStatus;
        return std::nullopt;
    }
    return parse_direct_guest_player_diagnosed(result.response.body,
                                               normal_video_id, last_failure_);
}


std::optional<HlsPlayerSource> parse_m3_guest_hls_source(
    std::string_view body, std::string_view expected_normal_video_id) {
    if (body.empty() || body.size() > 1024*1024 ||
        !ttnx::core::is_valid_video_id(expected_normal_video_id))
        return std::nullopt;
    Json parser(body);
    std::string status, playlist_url, aac_url;
    std::uint64_t aac_bitrate=0;
    VideoInfo info;
    unsigned seen=0;
    const bool success=parser.object([&](std::string_view key,unsigned depth) {
        if (key=="playabilityStatus") {
            return only_once(seen,1) && parse_status(parser,status,depth);
        }
        if (key=="videoDetails") {
            return only_once(seen,2) && parse_details(parser,info,depth);
        }
        if (key=="streamingData") {
            if (!only_once(seen,4)) return false;
            unsigned stream_seen=0;
            return parser.object([&](std::string_view attr,unsigned child_depth) {
                if (attr=="hlsManifestUrl") {
                    return only_once(stream_seen,1) &&
                           parser.text(&playlist_url,8192);
                }
                if (attr=="adaptiveFormats") {
                    if(!only_once(stream_seen,2))return false;
                    unsigned count=0;
                    return parser.array([&](unsigned entry_depth) {
                        if(++count>200)return false;
                        RawFormat f;
                        if(!parse_format(parser,f,entry_depth))return false;
                        const bool safe=f.mime.starts_with("audio/mp4") &&
                            f.mime.find("mp4a.40.")!=std::string::npos &&
                            !f.cipher && !f.drm && !f.color_info &&
                            f.bitrate>0 && f.bitrate<=1'000'000 &&
                            is_reviewable_googlevideo_https_url(f.url);
                        if(safe && f.bitrate>aac_bitrate) {
                            aac_bitrate=f.bitrate;
                            aac_url=f.url;
                        }
                        return true;
                    },child_depth);
                }
                return parser.skip(child_depth);
            },depth);
        }
        // Never materialize ads, tracking endpoints, thumbnails or comments.
        return parser.skip(depth);
    });
    if (!success || !parser.done() || seen!=7 || status!="OK" ||
        info.video_id!=expected_normal_video_id ||
        info.short_video || info.live ||
        info.duration_sec==0 || info.duration_sec>7ULL*24*3600 ||
        !is_reviewable_hls_manifest_url(playlist_url))
        return std::nullopt;
    HlsPlayerSource source;
    source.video_id=info.video_id;
    source.duration_ms=info.duration_sec*1000;
    source.master_playlist_url=std::move(playlist_url);
    source.external_aac_url=std::move(aac_url);
    return source;
}

std::optional<HlsPlayerSource> VisionHlsSource::resolve(
    std::string_view normal_video_id) {
    last_error_=HlsSourceError::InvalidRequest;
    status_=0;
    const auto request=youtube::make_m3_visionos_player_request(
        web_guest_,normal_video_id);
    if(!request)return std::nullopt;
    const auto response=http_.perform(*request);
    if(!response.ok) {
        last_error_=HlsSourceError::Transport;
        return std::nullopt;
    }
    status_=response.response.status_code;
    if(status_!=200) {
        last_error_=HlsSourceError::HttpStatus;
        return std::nullopt;
    }
    const auto parsed=parse_m3_guest_hls_source(
        response.response.body,normal_video_id);
    if(!parsed) {
        last_error_=HlsSourceError::ResponseRejected;
        return std::nullopt;
    }
    last_error_=HlsSourceError::None;
    return parsed;
}

} // namespace ttnx::playback
