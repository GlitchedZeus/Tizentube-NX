#include "tizentube_nx/net/http1_media_stream.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
using ttnx::net::Http1MediaStream;
static std::string response(std::string_view payload,std::string extra=""){
  return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(payload.size())+
    "\r\nContent-Type: video/mp2t\r\n"+extra+"\r\n"+std::string(payload);
}
int main(){
 const std::string payload(188*300,'x');
 for(std::size_t step:{1u,3u,7u,188u,1024u,32768u}) {
  const auto wire=response(payload,"Content-Encoding: identity\r\n");
  Http1MediaStream p;std::string out;
  for(std::size_t i=0;i<wire.size();i+=step)
    assert(p.feed(std::string_view(wire).substr(i,std::min(step,wire.size()-i)),
      [&](std::string_view chunk){out.append(chunk);return true;}));
  assert(p.finish() && p.received()==payload.size() && out==payload);
  assert(!p.feed("a",[](auto){return true;}));
 }
 const auto check=[](std::string wire,bool expected){
  Http1MediaStream p;std::string out;
  bool ok=true;
  for(std::size_t i=0;i<wire.size() && ok;i+=250)
   ok=p.feed(std::string_view(wire).substr(i,std::min<std::size_t>(250,wire.size()-i)),
       [&](std::string_view c){out.append(c);return true;});
  if(ok)ok=p.finish();
  assert(ok==expected);
  return out;
 };
 assert(check(response(payload),true)==payload);
 // Accurate byte quota: header callback MUST run before the first body
 // byte is delivered, independent of network fragmentation.
 for(std::size_t step:{1u,71u,32768u}) {
   int admissions=0, body_calls=0;
   const auto wire=response(payload);
   Http1MediaStream parser([&](std::size_t n) {
     ++admissions;
     assert(body_calls==0);
     return n==payload.size();
   });
   for(std::size_t i=0;i<wire.size();i+=step) {
     assert(parser.feed(std::string_view(wire).substr(
       i,std::min(step,wire.size()-i)), [&](std::string_view bytes) {
         ++body_calls; return !bytes.empty();
       }));
   }
   assert(parser.finish() && admissions==1 && body_calls>0);
 }
 {bool emitted=false;
  Http1MediaStream reject([](std::size_t){return false;});
  assert(!reject.feed(response(payload),[&](std::string_view){
     emitted=true;return true;
  }));
  assert(!emitted && reject.state()==Http1MediaStream::State::Rejected);
 }

 // Delimiter split exactly before a full 32 KiB media-containing TLS read.
 {Http1MediaStream p;const auto wire=response(payload);
  const auto edge=wire.find("\r\n\r\n")+2;
  std::string out;
  assert(p.feed(wire.substr(0,edge),[&](std::string_view b){out.append(b);return true;}));
  for(std::size_t i=edge;i<wire.size();i+=32768)
    assert(p.feed(std::string_view(wire).substr(i,std::min<std::size_t>(32768,wire.size()-i)),
        [&](std::string_view b){out.append(b);return true;}));
  assert(p.finish() && out==payload);
 }
 auto w=response(payload);w.replace(9,3,"302");assert(check(w,false).empty());
 assert(check(response(payload,"Content-Length: 42\r\n"),false).empty());
 assert(check(response(payload,"Transfer-Encoding: chunked\r\n"),false).empty());
 assert(check(response(payload,"Location: https://example.com/\r\n"),false).empty());
 assert(check(response(payload,"Content-Encoding: gzip\r\n"),false).empty());
 assert(check(response(payload,"Content-Range: bytes 0-3/4\r\n"),false).empty());
 w=response(payload);w.replace(w.find("video/mp2t"),10,"text/html ");assert(check(w,false).empty());
 w=response(payload);w.replace(w.find("Content-Length: ")+16,5,"99999");assert(!check(w,false).empty());
 w=response(payload);w.pop_back();assert(!check(w,false).empty()); // truncated, body never accepted as final
 assert(check(response(payload)+"extra",false).empty()==false); // emits at most exact body
 {Http1MediaStream p;auto wire=response(payload);assert(!p.feed(wire.substr(0,32768),
   [](auto){return false;}));assert(p.state()==Http1MediaStream::State::Rejected);}
 {Http1MediaStream p;assert(!p.feed(std::string(32769,'x'),[](auto){return true;}));}
 return 0;
}
