#include "media_file_sink.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
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
    // Horizon SD rename may not overwrite a destination that already exists.
    // A real prior completed AAC must survive either a successful
    // replacement or an injected promotion/fsync fault.
    const std::string audio_path=SwitchMediaCacheSink::kAdaptiveAudio;
    auto read_audio=[&](){
        std::ifstream in(audio_path,std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    };
    const auto old=body;
    std::string newer=body;
    newer.back()='Z';
    check(stage(Slot::AdaptiveAudio,newer) &&
          read_audio()==newer &&
          !fs::exists(audio_path+".previous"),
          "existing AAC destination safely replaced without rename-overwrite");
    {
        SwitchMediaCacheSink failing(Slot::AdaptiveAudio);
        SwitchMediaCacheSink::test_fail_on(SwitchMediaCacheSink::StorageStep::Promote);
        check(failing.begin() && failing.append(body) &&
              !failing.finish() &&
              failing.last_storage_error()==
                  SwitchMediaCacheSink::StorageStep::Promote &&
              read_audio()==newer &&
              !fs::exists(audio_path+".previous"),
              "promotion failure restores previous AAC before exposing media");
        SwitchMediaCacheSink::test_fail_on(SwitchMediaCacheSink::StorageStep::None);
    }
    {
        SwitchMediaCacheSink failing(Slot::AdaptiveAudio);
        SwitchMediaCacheSink::test_fail_on(SwitchMediaCacheSink::StorageStep::Sync);
        check(failing.begin() && failing.append(body) &&
              !failing.finish() &&
              failing.last_storage_error()==
                  SwitchMediaCacheSink::StorageStep::Sync &&
              read_audio()==newer,
              "fsync failure is specifically classified and old AAC retained");
        SwitchMediaCacheSink::test_fail_on(SwitchMediaCacheSink::StorageStep::None);
    }
    {
        const auto backup=audio_path+".previous";
        check(std::rename(audio_path.c_str(),backup.c_str())==0,
              "interrupted replacement leaves a prior backup");
        SwitchMediaCacheSink resumed(Slot::AdaptiveAudio);
        check(resumed.begin() && read_audio()==newer,
              "cache begin restores previous audio after interrupted rename");
        resumed.abort();
    }
    check(stage(Slot::AdaptiveVideo,std::string(2048,'X')) &&
          !SwitchMediaCacheSink::publish_adaptive_pair() &&
          !SwitchMediaCacheSink::adaptive_pair_valid(),
          "corrupted MP4 container cannot publish even when length plausible");
    check(!stage(Slot::AdaptiveAudio,{}),
          "empty or cancelled audio can never become verified");
    // Session cache must not accumulate past videos (or interrupted .part
    // and .previous copies), including after a failed second download.
    check(SwitchMediaCacheSink::discard_session_media() &&
          !fs::exists(SwitchMediaCacheSink::kAdaptiveAudio) &&
          !fs::exists(SwitchMediaCacheSink::kAdaptiveVideo) &&
          !fs::exists(SwitchMediaCacheSink::kAdaptiveMarker),
          "clean exit discards both adaptive media files and marker");
    const fs::path folder=SwitchMediaCacheSink::kFolder;
    fs::create_directories(folder);
    {
        std::ofstream(folder/"active.mp4.part")<<"partial";
        std::ofstream(folder/"adaptive_audio.m4a.previous")<<"old";
        std::ofstream(folder/"unrelated-settings.ini")<<"keep";
        fs::create_directories(folder/"do-not-delete");
        ttnx::playback::HlsPlaybackLease active;
        check(active.acquire() &&
              !SwitchMediaCacheSink::discard_session_media() &&
              fs::exists(folder/"active.mp4.part"),
              "active player lease blocks even cleanup of partial media");
    }
    check(SwitchMediaCacheSink::discard_session_media() &&
          !fs::exists(folder/"active.mp4.part") &&
          !fs::exists(folder/"adaptive_audio.m4a.previous") &&
          fs::exists(folder/"unrelated-settings.ini") &&
          fs::exists(folder/"do-not-delete"),
          "restart sweep removes crash leftovers, preserves unrelated files");
    // No ancestor of the app-owned media folder may be a symlink.
    // Repeated videos must not build an accumulating watch library.
    // Keep this test intentionally small/fast while exercising fsync,
    // marker validation and the real bounded unlink implementation.
    for(int round=0;round<20;++round) {
        const bool played=stage(Slot::AdaptiveAudio,body) &&
                          stage(Slot::AdaptiveVideo,body) &&
                          SwitchMediaCacheSink::publish_adaptive_pair();
        const bool cleaned=SwitchMediaCacheSink::discard_session_media();
        check(played && cleaned &&
              !fs::exists(SwitchMediaCacheSink::kAdaptiveVideo) &&
              !fs::exists(SwitchMediaCacheSink::kAdaptiveAudio) &&
              !fs::exists(SwitchMediaCacheSink::kAdaptiveMarker),
              "repeated watched videos leave zero persistent media files");
    }
    {
        const auto outside=fs::path("/tmp/ttnx-adaptive-cache-test/safe-target");
        std::ofstream(outside)<<"do not change";
        std::ofstream(folder/"active.mp4")<<"existing video";
        std::ofstream(folder/"adaptive_audio.m4a.part")<<"partial audio";
        fs::create_symlink(outside,folder/"active.mp4.part");
        check(!SwitchMediaCacheSink::discard_session_media() &&
              fs::exists(folder/"active.mp4") &&
              fs::exists(folder/"adaptive_audio.m4a.part") &&
              fs::exists(outside),
              "MP4 purge preflights all fixed paths before deleting any track");
        fs::remove(folder/"active.mp4.part");
        check(SwitchMediaCacheSink::discard_session_media() &&
              !fs::exists(folder/"active.mp4") &&
              !fs::exists(folder/"adaptive_audio.m4a.part") &&
              fs::exists(outside),
              "MP4 purge resumes after symlink interference is removed");
    }
    fs::remove_all(folder/"do-not-delete");
    fs::remove(folder/"unrelated-settings.ini");
    fs::remove_all(folder);
    const auto outside=fs::path("/tmp/ttnx-adaptive-cache-test/external-media");
    fs::create_directories(outside);
    std::ofstream(outside/"active.mp4")<<"do not erase";
    fs::create_directory_symlink(outside,folder);
    check(!SwitchMediaCacheSink::discard_session_media() &&
          fs::exists(outside/"active.mp4"),
          "symlink cache root is rejected; unrelated data preserved");
    fs::remove(folder);
    // A symlinked ancestor is also forbidden, even with a real cache root.
    fs::remove_all("/tmp/ttnx-adaptive-cache-test/switch");
    const auto foreign=fs::path("/tmp/ttnx-adaptive-cache-test/foreign");
    fs::create_directories(foreign/"TizenTube-NX/media-cache");
    std::ofstream(foreign/"TizenTube-NX/media-cache/active.mp4")<<"foreign";
    fs::create_directory_symlink(foreign,"/tmp/ttnx-adaptive-cache-test/switch");
    SwitchMediaCacheSink guarded(Slot::MuxedVideo);
    check(!SwitchMediaCacheSink::discard_session_media() &&
          !guarded.begin() &&
          fs::exists(foreign/"TizenTube-NX/media-cache/active.mp4"),
          "symlinked MP4 ancestor blocks cleanup and new downloads");
    fs::remove("/tmp/ttnx-adaptive-cache-test/switch");
    fs::remove_all("/tmp/ttnx-adaptive-cache-test");
    return errors?EXIT_FAILURE:EXIT_SUCCESS;
}
