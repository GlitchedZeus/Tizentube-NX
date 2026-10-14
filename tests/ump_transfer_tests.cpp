#include "tizentube_nx/playback/ump_transfer.hpp"
#include "tizentube_nx/net/http.hpp"
#include <cstdlib>
#include <iostream>
using namespace ttnx;
namespace {
int fails=0;
void ck(bool c,const char* n){if(!c){++fails;std::cerr<<"FAIL "<<n<<"\n";}}
struct Fake final:net::HttpClient {
    int calls{0}; net::HttpRequest last;
    net::HttpResult reply;
    net::HttpResult perform(const net::HttpRequest& r) override {
        ++calls;last=r;return reply;
    }
};
const std::string good="https://r1.googlevideo.com/videoplayback?expire=123&sig=signed";
void test() {
    Fake fake;
    playback::UmpTransferReader reader(fake);
    ck(reader.read("https://accounts.nintendo.com/videoplayback",0,100,1).failure==
       playback::UmpTransferFailure::InvalidUrlOrRange,"Nintendo rejected before POST");
    ck(fake.calls==0,"unsafe url never reaches transport");
    fake.reply.ok=true;
    fake.reply.response.status_code=200;
    fake.reply.response.body=std::string({21,5,0,'f','t','y','p'});
    fake.reply.response.headers={{"Content-Type","application/vnd.yt-ump"}};
    auto r=reader.read(good,0,1024,1);
    ck(r.ok() && r.bytes.size()==4 && r.bytes[0]=='f',"MEDIA bytes decoded");
    ck(fake.calls==1 && fake.last.method==net::HttpMethod::Post &&
       fake.last.body.size()==2 && fake.last.body[0]==0x78 && fake.last.body[1]==0,
       "fixed 2-byte UMP POST without arbitrary request body");
    ck(fake.last.url.find("ump=1")!=std::string::npos &&
       fake.last.url.find("range=0-1023")!=std::string::npos,
       "bounded query appended to signed source");
    ck(fake.last.headers.size()==3 && fake.last.headers[0].value=="application/x-protobuf",
       "fixed MIME headers");
    fake.reply.ok=false;
    ck(reader.read(good,0,1024,1).failure==
       playback::UmpTransferFailure::TransportRejected,"TLS error fail closed");
    fake.reply.ok=true;fake.reply.response.status_code=403;
    ck(reader.read(good,0,1024,1).failure==
       playback::UmpTransferFailure::HttpRejected,"403 not interpreted as playable video");
    fake.reply.response.status_code=200;
    fake.reply.response.body="<html>";
    ck(reader.read(good,0,1024,1).failure==
       playback::UmpTransferFailure::MissingMedia,"HTML body rejected");
    fake.reply.response.body=std::string({21,5,0,'f','t','y','p'});
    fake.reply.response.headers={{"Content-Type","text/html"}};
    ck(reader.read(good,0,1024,1).failure==
       playback::UmpTransferFailure::InvalidEnvelope,"HTML MIME rejected");
}
} // namespace
int main(){test();return fails?EXIT_FAILURE:EXIT_SUCCESS;}
