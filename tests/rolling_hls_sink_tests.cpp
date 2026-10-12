#include "rolling_hls_sink.hpp"
#include "tizentube_nx/playback/local_live_hls_gate.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
namespace fs=std::filesystem;
using ttnx::switch_app::RollingHlsSink;
namespace {int failures=0;void ck(bool b,const char* s){if(!b){++failures;std::cerr<<"FAIL: "<<s<<'\n';}}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
}
int main(){
  const fs::path root=RollingHlsSink::kRoot;
  fs::remove_all("/tmp/ttnx-m4-rolling-test");
  const std::string packet(188*2,'T'); // Native caller must verify codecs before append.
  RollingHlsSink sink;
  ck(!sink.begin(60001),"reject malformed manifest target duration");
  ck(sink.begin(5000),"begin fresh ephemeral rolling directory");
  ck(sink.append(0,3000,packet),"fsync and publish first verified segment");
  ck(!sink.append(0,3000,packet),"duplicate segment rejected");
  ck(!sink.append(2,3000,packet),"segment gap rejected");
  ck(!sink.append(1,0,packet),"invalid zero duration rejected");
  ck(!sink.append(1,3000,std::string(17*1024*1024,'v')),"unbounded media chunk rejected");
  ck(!sink.append(1,6000,packet),"reject segment beyond fixed HLS target");
  ck(sink.append(1,3000,packet),"second segment published");
  const auto first=read(root/"stream.m3u8");
  ttnx::playback::LocalLiveHlsGate prefix{};
  ck(ttnx::playback::parse_local_live_hls(first,prefix) &&
     prefix.playable_prefix() && prefix.first_sequence==0,
     "persisted sink playlist passes exact local-only mpv pre-open gate");
  ck(first.find("#EXT-X-TARGETDURATION:5")!=std::string::npos &&
     first.find("#EXT-X-MEDIA-SEQUENCE:0")!=std::string::npos &&
     first.find("v_00000.ts")!=std::string::npos &&
     first.find("v_00001.ts")!=std::string::npos &&
     first.find("#EXT-X-ENDLIST")==std::string::npos,
     "growing playlist references only durable local files");
  ck(!sink.retire_before(2),"cannot retire entire visible playback window");
  ck(sink.retire_before(1),"advance window before unlinking consumed chunk");
  ck(!sink.retire_before(1),"retiring zero new segments is refused");
  const auto next=read(root/"stream.m3u8");
  ck(ttnx::playback::parse_local_live_hls(next,prefix) &&
     prefix.playable_prefix() && prefix.first_sequence==1,
     "retired sliding playlist remains acceptable at media-sequence 1");
  ck(next.find("#EXT-X-TARGETDURATION:5")!=std::string::npos &&
     next.find("#EXT-X-MEDIA-SEQUENCE:1")!=std::string::npos &&
     next.find("v_00000.ts")==std::string::npos &&
     !fs::exists(root/"v_00000.ts") &&
     sink.bytes()==packet.size(),"rolling playlist sheds already played bytes");
  ck(sink.finish() && read(root/"stream.m3u8").find("#EXT-X-ENDLIST")!=std::string::npos &&
     ttnx::playback::parse_local_live_hls(read(root/"stream.m3u8"),prefix) &&
     prefix.complete,
     "finished playlist closes cleanly");
  ck(!sink.finish() && !sink.append(2,3000,packet),
     "finalized VOD cannot silently append more media");
  ck(RollingHlsSink::discard() && !fs::exists(root),"end of playback leaves no media");
  ck(RollingHlsSink::discard(),"idempotent no-media cleanup");
  // A poisoned manifest destination simulates rename/publish failure.
  // The new segment must be unlinked and must not consume invisible quota.
  {
    RollingHlsSink transactional;
    ck(transactional.begin(5000) && transactional.append(0,3000,packet),
       "transactional fixture begins with a published segment");
    const auto foreign=root.parent_path()/"manifest-foreign.txt";
    std::ofstream(foreign)<<"external data";
    fs::remove(root/"stream.m3u8");
    fs::create_symlink(foreign,root/"stream.m3u8");
    ck(!transactional.append(1,3000,packet) &&
       !fs::exists(root/"v_00001.ts") &&
       transactional.next_index()==1 &&
       transactional.bytes()==packet.size() &&
       read(foreign)=="external data",
       "manifest publish refusal rolls back newly written media and quota");
    fs::remove(root/"stream.m3u8");
    ck(transactional.append(1,3000,packet),
       "clean destination permits retry of the same segment");
    ck(transactional.finish(),"transactional stream finalizes");
    ck(transactional.retire_before(1) &&
       read(root/"stream.m3u8").find("#EXT-X-ENDLIST")!=std::string::npos,
       "retiring a segment from a completed video preserves ENDLIST");
    ck(transactional.close_session() && !fs::exists(root),
       "instance teardown resets internal accounting and finished state");
    ck(transactional.begin() && transactional.append(0,3000,packet),
       "reused sink writes a new clip after cleanup");
    ck(transactional.close_session() && !fs::exists(root),
       "second session also leaves zero media bytes");
    fs::remove(foreign);
  }
  // Native FAT path: rename/promote failure must restore the previously
  // published local playlist and roll back all unadvertised media bytes.
  {
    RollingHlsSink rollback;
    ck(rollback.begin(5000) && rollback.append(0,3000,packet),
       "FAT promotion fixture publishes original segment");
    const auto original=read(root/"stream.m3u8");
    RollingHlsSink::test_fail_manifest_promotion(true);
    ck(!rollback.append(1,3000,packet) &&
       read(root/"stream.m3u8")==original &&
       !fs::exists(root/"v_00001.ts") &&
       !fs::exists(root/"stream.m3u8.previous") &&
       rollback.next_index()==1 && rollback.bytes()==packet.size(),
       "FAT promotion rejection restores playable manifest without SD growth");
    RollingHlsSink::test_fail_manifest_promotion(false);
    ck(rollback.append(1,3000,packet) &&
       !fs::exists(root/"stream.m3u8.previous"),
       "recovered FAT promotion publishes second segment and cleans backup");
    ck(rollback.close_session() && !fs::exists(root),
       "FAT rollback video teardown returns media usage to zero");
  }
  RollingHlsSink hour;
  ck(hour.begin(),"long-form session starts");
  const std::string chunk(48*1024,'Z');
  for(std::size_t i=0;i<1200;++i){
    ck(hour.append(i,3000,chunk),"hour-long HLS segment write and publish");
    if(i>=3)ck(hour.retire_before(i-1),"hour-long segment eviction after playhead");
    ck(hour.bytes()<=RollingHlsSink::kMaxMediaBytes && hour.active_segments()<=3,
       "hour-long session stays inside bounded SD ring");
  }
  ck(hour.finish(),"hour-long live manifest becomes final VOD");
  ck(RollingHlsSink::discard() && !fs::exists(root),"hour-long playback leaves zero media");
  ck(sink.begin()==false,"active sink can't silently restart over foreign session");
  fs::create_directories(root);
  std::ofstream(root/"v_00000.ts")<<"app owned";
  std::ofstream(root/"my_notes.txt")<<"leave untouched";
  ck(!RollingHlsSink::discard() && fs::exists(root/"v_00000.ts") &&
     fs::exists(root/"my_notes.txt"),"unknown file aborts entire cleanup preflight");
  fs::remove(root/"my_notes.txt");
  ck(RollingHlsSink::discard(),"valid cleanup succeeds after poison removed");
  fs::create_directories(root);
  std::ofstream(root/"v_00000.ts")<<"hold";
  {ttnx::playback::HlsPlaybackLease lease;
   ck(lease.acquire() && !RollingHlsSink::discard() && fs::exists(root/"v_00000.ts"),
      "active media decoder lease blocks deletion");}
  ck(RollingHlsSink::discard(),"release decoder lease then clear storage");
  const fs::path foreign="/tmp/ttnx-m4-rolling-foreign";
  fs::remove_all(foreign);
  fs::create_directories(foreign);
  std::ofstream(foreign/"v_00000.ts")<<"foreign";
  fs::create_directories(root.parent_path());
  fs::create_directory_symlink(foreign,root);
  ck(!RollingHlsSink::discard() && !RollingHlsSink().begin() &&
     fs::exists(foreign/"v_00000.ts"),"symlinked media root cannot redirect write or deletion");
  fs::remove(root);
  fs::remove_all("/tmp/ttnx-m4-rolling-test");
  fs::remove_all(foreign);
  if(failures)std::cerr<<failures<<" rolling HLS sink failures\n";
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
