#include "tizentube_nx/playback/rolling_stream_epoch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
namespace fs=std::filesystem;
using ttnx::playback::RollingStreamEpoch;
struct FileReader {
    std::string filepath;
    struct Reply {bool ok{};std::size_t bytes{};};
    template<class Admit,class Emit>
    Reply stream(std::string_view url,Admit admit,Emit emit) {
        if(url.find("segment_")!=0)return {};
        const std::size_t bytes=fs::file_size(filepath);
        if(!admit(bytes))return {};
        std::ifstream file(filepath,std::ios::binary);
        if(!file)return {};
        char data[32768];std::size_t emitted=0;
        while(file) {
            file.read(data,sizeof(data));
            const auto got=file.gcount();
            if(got>0) {
                if(!emit(std::string_view(data,static_cast<std::size_t>(got))))return {};
                emitted+=static_cast<std::size_t>(got);
            }
        }
        return {file.eof(),emitted};
    }
};
int main(int argc,char** argv) {
    assert(argc==2);
    const auto path=fs::temp_directory_path()/"ttnx-real-media-epoch";
    fs::remove_all(path);fs::create_directory(path);
    auto reader=std::make_shared<FileReader>();reader->filepath=argv[1];
    const auto size=fs::file_size(reader->filepath);
    assert(size>376&&size<512*1024);
    auto epoch=RollingStreamEpoch::with_reader((path/"muxed.part").string(),
        {"segment_0","segment_1","segment_2"},reader,
        [](std::string_view url){return url.find("segment_")==0;},
        2*size);
    assert(epoch&&epoch->start());
    for(int i=0;i<5000 && epoch->snapshot().phase!=RollingStreamEpoch::Phase::Backpressure;++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    auto snap=epoch->snapshot();
    assert(snap.phase==RollingStreamEpoch::Phase::Backpressure);
    assert(snap.segments_committed==2 && snap.peak_file_bytes<=2*size);
    mpv_handle handle{};
    assert(epoch->register_decoder(&handle));
    mpv_stream_cb_info cookie{};
    char uri[]="ttnxring://media";
    assert(g_open_fn(g_user_data,uri,&cookie)==0);
    std::ifstream original(reader->filepath,std::ios::binary);
    std::string actual(size,'\0');std::size_t current=0;
    while(current<size) {
        const auto n=cookie.read_fn(cookie.cookie,actual.data()+current,size-current);
        assert(n>0);current+=static_cast<std::size_t>(n);
    }
    std::string expected(size,'\0');original.read(expected.data(),size);
    assert(original.gcount()==static_cast<std::streamsize>(size));
    assert(actual==expected);
    epoch->stop_and_join();
    assert(!epoch->notify_decoder_destroyed());
    cookie.close_fn(cookie.cookie);
    assert(epoch->notify_decoder_destroyed()&&epoch->cleanup());
    assert(!fs::exists(path/"muxed.part"));
    assert(epoch->restart_at(2)&&epoch->start());
    for(int i=0;i<5000 && epoch->snapshot().phase!=RollingStreamEpoch::Phase::Complete;++i)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    assert(epoch->snapshot().segments_committed==1);
    epoch->stop_and_join();
    assert(epoch->cleanup() && !fs::exists(path/"muxed.part"));
    epoch.reset();reader.reset();fs::remove_all(path);
}
