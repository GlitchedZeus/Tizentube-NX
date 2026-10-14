// Host-only LIVE integration probe of the EXACT native TS and AAC-MP4
// validators. Takes two private ephemeral paths and never logs them.
// This does not include Switch services, libmpv or any network permissions.
#include "tizentube_nx/playback/ts_streams.hpp"
#include "tizentube_nx/playback/aac_mp4_structure.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

int main(int argc,char** argv) {
    if(argc!=3)return 1;
    std::ifstream video(argv[1],std::ios::binary);
    if(!video)return 2;
    std::array<char,65536> sample{};
    video.read(sample.data(),static_cast<std::streamsize>(sample.size()));
    const auto length=video.gcount();
    if(length<376)return 3;
    const auto streams=ttnx::playback::inspect_mpeg_ts(
        std::string_view(sample.data(),static_cast<std::size_t>(length)));
    if(!streams.recognized || !streams.h264)return 4;

    std::ifstream audio(argv[2],std::ios::binary|std::ios::ate);
    if(!audio)return 5;
    const auto signed_length=audio.tellg();
    if(signed_length<1024 ||
       signed_length>static_cast<std::streamoff>(16ULL*1024*1024))
        return 6;
    const auto total=static_cast<std::uint64_t>(signed_length);
    audio.seekg(0);
    ttnx::playback::AacMp4Structure atoms;
    if(!atoms.begin(total))return 7;
    std::array<char,1048576> chunk{};
    std::uint64_t consumed=0;
    while(consumed<total) {
        audio.read(chunk.data(),static_cast<std::streamsize>(chunk.size()));
        const auto n=audio.gcount();
        if(n<=0)return 8;
        if(!atoms.append(std::string_view(chunk.data(),static_cast<std::size_t>(n))))
            return 9;
        consumed+=static_cast<std::uint64_t>(n);
    }
    if(consumed!=total || !atoms.complete())return 10;
    return 0;
}
