#include "tizentube_nx/library/local_library.hpp"
#ifdef NDEBUG
#undef NDEBUG // execute assertions even in Release CTest builds
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
using namespace ttnx::library;
int main() {
    assert(LocalLibrary::valid_id("aBCdef012_-"));
    assert(!LocalLibrary::valid_id("short"));
    assert(!LocalLibrary::valid_id("aaaaaaaaaa/"));
    LocalLibrary lib;
    lib.record_watch("aBCdef012_-",100,17);
    lib.record_watch("ABCDEFGHIJK",101,42);
    lib.record_watch("aBCdef012_-",102,33);
    assert(lib.history().size()==2 && lib.history()[0].last_watched==102);
    assert(lib.add_watch_later("aBCdef012_-"));
    assert(lib.add_watch_later("aBCdef012_-"));
    assert(lib.watch_later().size()==1);
    assert(lib.add_favorite("ABCDEFGHIJK"));
    assert(!lib.add_favorite("invalid"));
    assert(lib.follow_channel("UCaaaaaaaaaaaaaaaaaaaaaa"));
    assert(!lib.follow_channel("UCno"));
    assert(lib.favorites().size()==1 && lib.followed_channels().size()==1);
    auto data=lib.serialize();
    LocalLibrary loaded;
    assert(LocalLibrary::parse(data,loaded));
    assert(loaded.history()[0].resume_seconds==33);
    assert(loaded.watch_later().size()==1);
    assert(loaded.favorites().size()==1 && loaded.followed_channels().size()==1);
    assert(loaded.remove_favorite("ABCDEFGHIJK"));
    assert(loaded.unfollow_channel("UCaaaaaaaaaaaaaaaaaaaaaa"));
    auto tamper=data;tamper[20]='X';
    assert(!LocalLibrary::parse(tamper,loaded));
    assert(loaded.history().size()==2); // transactional parse
    lib.set_history_enabled(false);
    lib.record_watch("XXXXXXXXXXX",123);
    assert(lib.history().size()==2);
    lib.clear_history();
    assert(lib.history().empty());
    assert(lib.remove_watch_later("aBCdef012_-"));
    assert(!lib.remove_watch_later("aBCdef012_-"));
    for(unsigned i=0;i<1100;++i) {
        char id[12];std::snprintf(id,sizeof(id),"%011u",i);
        lib.set_history_enabled(true);
        lib.record_watch(id,100+i,i);
    }
    assert(lib.history().size()==LocalLibrary::kHistoryMax);
    const auto dir=std::filesystem::temp_directory_path()/"ttnx-library-host-fixture";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const auto path=(dir/"history.dat").string();
    assert(lib.save(path));
    LocalLibrary read;
    assert(read.load(path) && read.history().size()==1000);
    read.clear_history();assert(read.save(path));
    {std::ofstream corrupt(path,std::ios::binary|std::ios::trunc);corrupt<<"broken";}
    LocalLibrary restore;
    assert(restore.load(path) && restore.history().size()==1000);
    assert(restore.serialize().size()<LocalLibrary::kFileMaxBytes);
    restore.clear_history();assert(restore.save(path));
    LocalLibrary saved_again;
    assert(saved_again.load(path) && saved_again.history().empty());
    {std::ofstream corrupt(path,std::ios::binary|std::ios::trunc);corrupt<<"bad";}
    LocalLibrary surviving_backup;
    assert(surviving_backup.load(path) && surviving_backup.history().size()==1000);
    // Refuse app-controlled symlinks, including malicious backups/temps.
    std::filesystem::remove(path);
    std::filesystem::create_symlink("/etc/passwd",path);
    assert(!lib.save(path));
    std::filesystem::remove_all(dir);
}
