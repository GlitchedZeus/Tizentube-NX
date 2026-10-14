#include "tizentube_nx/playback/ring_orphan_cleanup.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
using ttnx::playback::RingOrphanCleanup;
int main() {
    const auto root=fs::temp_directory_path()/"ttnx-ring-recovery-test";
    fs::remove_all(root);fs::create_directory(root);
    const auto p=root/RingOrphanCleanup::kFilename;
    const auto unrelated=root/"important-save.txt";
    {std::ofstream out(unrelated);out<<"preserve";}
    assert(RingOrphanCleanup::at_startup(root.string(),true));
    {std::ofstream out(p);out<<"incomplete segment";}
    assert(!RingOrphanCleanup::at_startup(root.string(),false));
    assert(fs::exists(p));
    assert(RingOrphanCleanup::at_startup(root.string(),true));
    assert(!fs::exists(p)&&fs::exists(unrelated));
    fs::create_symlink(unrelated,p);
    assert(!RingOrphanCleanup::at_startup(root.string(),true));
    assert(fs::exists(unrelated));
    fs::remove(p);
    {std::ofstream out(p,std::ios::binary);
     out.seekp(ttnx::playback::RollingFileStream::kMaximumMedia);
     out.put('!');}
    assert(!RingOrphanCleanup::at_startup(root.string(),true));
    fs::remove(p);
    const auto link=fs::temp_directory_path()/"ttnx-ring-recovery-link";
    fs::remove(link);fs::create_directory_symlink(root,link);
    assert(!RingOrphanCleanup::at_startup(link.string(),true));
    fs::remove(link);
    fs::remove_all(root);
}
