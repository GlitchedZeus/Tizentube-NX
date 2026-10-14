#include "tizentube_nx/playback/bounded_direct_track.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace ttnx;
class FakeTransport final:public playback::MediaChunkTransport {
public:
    std::string source;
    std::uint64_t changed_size=0;
    unsigned calls=0;
    bool html=false, wrong_status=false;
    unsigned temporary_errors=0;
    int temporary_status=0;
    net::HttpResult perform(const net::HttpRequest& q)override {
        ++calls;
        if(temporary_errors) {
            --temporary_errors;
            net::HttpResult failure;
            failure.ok=false;
            failure.response.status_code=temporary_status;
            return failure;
        }
        net::HttpResult out;out.ok=true;
        const auto value=q.headers[0].value;
        assert(value.starts_with("bytes="));
        const auto dash=value.find('-');
        const auto first=std::stoull(value.substr(6,dash-6));
        const auto requested=std::stoull(value.substr(dash+1));
        const auto total=changed_size?changed_size:source.size();
        const auto last=std::min<std::uint64_t>(requested,total-1);
        out.response.status_code=wrong_status?200:206;
        out.response.body=source.substr(first,last-first+1);
        if(html)out.response.body="<html>"+out.response.body.substr(6);
        out.response.headers={{"Content-Type","video/mp4"},
            {"Content-Range","bytes "+std::to_string(first)+"-"+
             std::to_string(last)+"/"+std::to_string(total)},
            {"Content-Length",std::to_string(out.response.body.size())}};
        return out;
    }
};
int main() {
    FakeTransport fake;
    fake.source.resize(1024*1024+901,'A');
    fake.source.replace(0,12,std::string("\0\0\0\x18""ftypmp42",12));
    for(std::size_t i=12;i<fake.source.size();++i)
        fake.source[i]=static_cast<char>('a'+i%26);
    playback::MediaChunkReader http(fake);
    const std::string url="https://r1---sn-abcd.googlevideo.com/videoplayback?expire=9999999999&id=1";
    playback::BoundedDirectTrack invalid(http,"http://evil/videoplayback");
    assert(!invalid.prime() && invalid.fault()==playback::BoundedDirectTrack::Fault::Policy);
    playback::BoundedDirectTrack track(http,url);
    assert(track.prime());
    assert(track.size()==static_cast<std::int64_t>(fake.source.size()));
    assert(track.fetched_ranges()==1);
    assert(track.range_retries()==0);
    assert(fake.calls==1);
    assert(track.retained_bytes()==262144);
    char block[32768]{};
    assert(track.read_at(16,block,32768)==32768);
    assert(std::string_view(block,32768)==std::string_view(fake.source).substr(16,32768));
    assert(fake.calls==1);
    assert(track.read_at(262144,block,32768)==32768);
    assert(fake.calls==2);
    assert(track.retained_bytes()==524288);
    assert(track.read_at(22,block,100)==100); // A/B/A cached
    assert(fake.calls==2);
    assert(track.read_at(262144,block,32)==32); // A/B/A/B cached
    assert(fake.calls==2);
    assert(track.read_at(fake.source.size()-10,block,32)==10);
    assert(fake.calls==3);
    assert(track.retained_bytes()<=2*playback::BoundedDirectTrack::kWindow);
    assert(track.read_at(fake.source.size(),block,32)==0);
    assert(track.read_at(0,block,32769)==-1);
    track.cancel();
    assert(track.read_at(0,block,10)==-1);
    playback::BoundedDirectTrack changed(http,url);
    assert(changed.prime());
    fake.changed_size=fake.source.size()+8;
    // Changed CDN media length MUST NOT silently contaminate the stream.
    assert(changed.read_at(262144,block,128)==-1);
    assert(changed.fault()==playback::BoundedDirectTrack::Fault::ChangedResource);
    fake.changed_size=0;
    FakeTransport unsafe;unsafe.source=fake.source;unsafe.wrong_status=true;
    playback::MediaChunkReader bad_http(unsafe);
    playback::BoundedDirectTrack bad(bad_http,url);
    assert(!bad.prime()&&bad.fault()==playback::BoundedDirectTrack::Fault::Http);
    // Recover only a transient network failure, preserving the precise
    // byte-range and at most two buffered 256-KiB windows.
    FakeTransport flaky;flaky.source=fake.source;flaky.temporary_errors=1;
    playback::MediaChunkReader flaky_reader(flaky);
    playback::BoundedDirectTrack rescued(flaky_reader,url);
    assert(rescued.prime());
    assert(flaky.calls==2 && rescued.range_retries()==1);
    assert(rescued.retained_bytes()==playback::BoundedDirectTrack::kWindow);
    // One temporary 503 on a new range is independently retried.
    flaky.temporary_errors=1;flaky.temporary_status=503;
    assert(rescued.read_at(262144,block,64)==64);
    assert(flaky.calls==4 && rescued.range_retries()==2);
    // Permanent 403 and HTTP 200 are NEVER retried.
    FakeTransport forbidden;forbidden.source=fake.source;
    forbidden.temporary_errors=1;forbidden.temporary_status=403;
    playback::MediaChunkReader forbidden_reader(forbidden);
    playback::BoundedDirectTrack denied(forbidden_reader,url);
    assert(!denied.prime() && forbidden.calls==1);
    assert(denied.range_retries()==0);
    FakeTransport persistent;persistent.source=fake.source;
    persistent.temporary_errors=2;persistent.temporary_status=502;
    playback::MediaChunkReader persistent_reader(persistent);
    playback::BoundedDirectTrack exhausted(persistent_reader,url);
    assert(!exhausted.prime() && persistent.calls==2);
    assert(exhausted.range_retries()==1);
    FakeTransport wrong;wrong.source=std::string(512*1024,'a');
    playback::MediaChunkReader wrong_reader(wrong);
    playback::BoundedDirectTrack not_mp4(wrong_reader,url);
    assert(!not_mp4.prime()&&not_mp4.fault()==playback::BoundedDirectTrack::Fault::InvalidMp4);
}
