#include "tizentube_nx/playback/extraction_wire.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace ttnx::playback {
namespace {

class Reader {
public:
    explicit Reader(std::string_view data) : data_(data) {}
    void whitespace() {
        while (pos_ < data_.size() &&
               (data_[pos_] == ' ' || data_[pos_] == '\n' ||
                data_[pos_] == '\r' || data_[pos_] == '\t')) ++pos_;
    }
    bool consume(char token) {
        whitespace();
        if (pos_ >= data_.size() || data_[pos_] != token) return false;
        ++pos_;
        return true;
    }
    bool done() { whitespace(); return pos_ == data_.size(); }

    bool string(std::string& out, std::size_t limit = 8192) {
        if (!consume('"')) return false;
        out.clear();
        while (pos_ < data_.size()) {
            const unsigned char c = static_cast<unsigned char>(data_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20 || c > 0x7e) return false; // ASCII-only normalized wire
            if (c == '\\') {
                if (pos_ >= data_.size()) return false;
                const char escape = data_[pos_++];
                if (escape != '"' && escape != '\\' && escape != '/')
                    return false; // no Unicode/escaped controls in URLs or keys
                out.push_back(escape);
            } else out.push_back(static_cast<char>(c));
            if (out.size() > limit) return false;
        }
        return false;
    }

    bool number(std::uint64_t& out) {
        whitespace();
        if (pos_ >= data_.size() ||
            data_[pos_] < '0' || data_[pos_] > '9') return false;
        if (data_[pos_] == '0' && pos_ + 1 < data_.size() &&
            data_[pos_ + 1] >= '0' && data_[pos_ + 1] <= '9') return false;
        out = 0;
        while (pos_ < data_.size() && data_[pos_] >= '0' && data_[pos_] <= '9') {
            const unsigned d = static_cast<unsigned>(data_[pos_] - '0');
            if (out > (std::numeric_limits<std::uint64_t>::max() - d) / 10)
                return false;
            out = out * 10 + d;
            ++pos_;
        }
        return true;
    }

    bool small_number(unsigned& out) {
        std::uint64_t number_value = 0;
        if (!number(number_value) ||
            number_value > std::numeric_limits<unsigned>::max()) return false;
        out = static_cast<unsigned>(number_value);
        return true;
    }

    bool boolean(bool& out) {
        whitespace();
        if (data_.substr(pos_, 4) == "true") { pos_ += 4; out = true; return true; }
        if (data_.substr(pos_, 5) == "false") { pos_ += 5; out = false; return true; }
        return false;
    }

    bool object(const std::function<bool(std::string_view)>& field) {
        if (!consume('{')) return false;
        if (consume('}')) return true;
        while (true) {
            std::string key;
            if (!string(key, 64) || !consume(':') || !field(key)) return false;
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
    }

    bool array(const std::function<bool()>& item) {
        if (!consume('[')) return false;
        if (consume(']')) return true;
        while (true) {
            if (!item()) return false;
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }
private:
    std::string_view data_;
    std::size_t pos_{0};
};

bool once(unsigned& seen, unsigned bit) {
    if (seen & bit) return false;
    seen |= bit;
    return true;
}

bool parse_video_codec(std::string_view codec, VideoCodec& value) {
    if (codec == "h264") value = VideoCodec::H264;
    else if (codec == "vp9") value = VideoCodec::Vp9;
    else if (codec == "av1") value = VideoCodec::Av1;
    else if (codec == "unknown") value = VideoCodec::Unknown;
    else return false;
    return true;
}

bool parse_audio_codec(std::string_view codec, AudioCodec& value) {
    if (codec == "aac") value = AudioCodec::Aac;
    else if (codec == "opus") value = AudioCodec::Opus;
    else if (codec == "unknown") value = AudioCodec::Unknown;
    else return false;
    return true;
}

bool parse_video(Reader& in, VideoFormat& video) {
    unsigned seen = 0;
    const bool ok = in.object([&](std::string_view key) {
        if (key == "url")
            return once(seen, 1) && in.string(video.url);
        if (key == "codec") {
            std::string s;
            return once(seen, 2) && in.string(s, 24) &&
                   parse_video_codec(s, video.codec);
        }
        if (key == "muxed_audio") {
            std::string s;
            return once(seen, 4) && in.string(s, 24) &&
                   parse_audio_codec(s, video.muxed_audio);
        }
        if (key == "width")
            return once(seen, 8) && in.small_number(video.width);
        if (key == "height")
            return once(seen, 16) && in.small_number(video.height);
        if (key == "fps")
            return once(seen, 32) && in.small_number(video.fps);
        if (key == "bit_depth")
            return once(seen, 64) && in.small_number(video.bit_depth);
        if (key == "bitrate")
            return once(seen, 128) && in.number(video.bitrate);
        if (key == "hdr")
            return once(seen, 256) && in.boolean(video.hdr);
        if (key == "drm")
            return once(seen, 512) && in.boolean(video.drm);
        return false;
    });
    return ok && seen == 1023;
}

bool parse_audio(Reader& in, AudioFormat& audio) {
    unsigned seen = 0;
    const bool ok = in.object([&](std::string_view key) {
        if (key == "url") return once(seen, 1) && in.string(audio.url);
        if (key == "codec") {
            std::string s;
            return once(seen, 2) && in.string(s, 24) &&
                   parse_audio_codec(s, audio.codec);
        }
        if (key == "bitrate")
            return once(seen, 4) && in.number(audio.bitrate);
        if (key == "drm")
            return once(seen, 8) && in.boolean(audio.drm);
        return false;
    });
    return ok && seen == 15;
}

} // namespace

std::optional<ExtractionResult> parse_extraction_wire_response(
    std::string_view json) {
    if (json.empty() || json.size() > 256 * 1024) return std::nullopt;
    Reader in(json);
    ExtractionResult result;
    unsigned seen = 0;
    const bool ok = in.object([&](std::string_view key) {
        if (key == "video_id")
            return once(seen, 1) && in.string(result.video_id, 11);
        if (key == "duration_ms")
            return once(seen, 2) && in.number(result.duration_ms);
        if (key == "is_live")
            return once(seen, 4) && in.boolean(result.is_live);
        if (key == "is_short")
            return once(seen, 8) && in.boolean(result.is_short);
        if (key == "video")
            return once(seen, 16) && in.array([&]() {
                if (result.video.size() >= 200) return false;
                VideoFormat f;
                if (!parse_video(in, f)) return false;
                result.video.push_back(std::move(f));
                return true;
            });
        if (key == "audio")
            return once(seen, 32) && in.array([&]() {
                if (result.audio.size() >= 200) return false;
                AudioFormat f;
                if (!parse_audio(in, f)) return false;
                result.audio.push_back(std::move(f));
                return true;
            });
        return false;
    });
    if (!ok || seen != 63 || !in.done()) return std::nullopt;
    // The native selector is the ultimate content/host policy gate.
    return result;
}

} // namespace ttnx::playback
