#include "media_file_sink.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
namespace {
int errors=0;
void check(bool ok,const char* message){
    if(!ok){++errors;std::cerr<<"FAIL "<<message<<"\n";}
}
std::string box(const char* type,const std::string& data){
    std::string out;
    const std::uint32_t length=static_cast<std::uint32_t>(8+data.size());
    for(int i=3;i>=0;--i)out.push_back(static_cast<char>((length>>(i*8))&255));
    out.append(type,4);
    out.append(data);
    return out;
}
std::string valid_mp4() {
    std::string data=box("ftyp",std::string("isom")+std::string(8,'x'));
    data+=box("moov",std::string(24,'m'));
    data+=box("mdat",std::string(2000,'a'));
    return data;
}
}
int main(){
    using ttnx::switch_app::SwitchMediaCacheSink;
    using Slot=SwitchMediaCacheSink::Slot;
    fs::remove_all("/tmp/ttnx-adaptive-cache-test");
    const auto body=valid_mp4();
    auto stage=[&](Slot slot,std::string data)->bool {
        SwitchMediaCacheSink sink(slot);
        return sink.begin()&&sink.append(data)&&sink.finish();
    };
    check(stage(Slot::AdaptiveAudio,body)&&
          !SwitchMediaCacheSink::adaptive_pair_valid(),
          "audio alone never publishes an adaptive pair");
    check(stage(Slot::AdaptiveVideo,body)&&
          !SwitchMediaCacheSink::adaptive_pair_valid(),
          "video alone cannot publish before explicit two-file commit");
    check(SwitchMediaCacheSink::publish_adaptive_pair()&&
          SwitchMediaCacheSink::adaptive_pair_valid(),
          "valid two MP4 structures and sizes commit atomically");
    const auto marker=std::string(SwitchMediaCacheSink::kAdaptiveMarker);
    check(fs::exists(marker),"verified marker committed to disk");
    {
        ttnx::playback::HlsPlaybackLease watching;
        check(watching.acquire(),"libmpv exclusive playback lease acquired");
        SwitchMediaCacheSink another(Slot::AdaptiveVideo);
        SwitchMediaCacheSink::invalidate_adaptive_pair();
        check(!another.begin() && !SwitchMediaCacheSink::publish_adaptive_pair() &&
              SwitchMediaCacheSink::adaptive_pair_valid() &&
              fs::exists(marker),
              "active libmpv prevents replacement and marker invalidation");
    }
    check(stage(Slot::AdaptiveAudio,body)&&
          !SwitchMediaCacheSink::adaptive_pair_valid(),
          "new transaction invalidates old pair before first byte");
    check(SwitchMediaCacheSink::publish_adaptive_pair()&&
          SwitchMediaCacheSink::adaptive_pair_valid(),
          "retry publishes new matched complete pair");
    const auto old=body;
    check(stage(Slot::AdaptiveVideo,std::string(2048,'X')) &&
          !SwitchMediaCacheSink::publish_adaptive_pair() &&
          !SwitchMediaCacheSink::adaptive_pair_valid(),
          "corrupted MP4 container cannot publish even when length plausible");
    check(!stage(Slot::AdaptiveAudio,{}),
          "empty or cancelled audio can never become verified");
    fs::remove_all("/tmp/ttnx-adaptive-cache-test");
    return errors?EXIT_FAILURE:EXIT_SUCCESS;
}
