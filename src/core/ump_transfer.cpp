#include "tizentube_nx/playback/ump_transfer.hpp"
#include "tizentube_nx/playback/ump_wire.hpp"
#include <string>
#include <utility>

namespace ttnx::playback {
UmpTransferResult UmpTransferReader::read(const std::string& signed_url,
                                          std::uint64_t start,
                                          std::uint64_t resource_size,
                                          int request_number) {
    UmpTransferResult result;
    const auto uri=reviewed_ump_chunk_url(signed_url,start,resource_size,request_number);
    if(!uri) {
        result.failure=UmpTransferFailure::InvalidUrlOrRange;
        return result;
    }
    net::HttpRequest req;
    req.method=net::HttpMethod::Post;
    req.url=*uri;
    req.timeout_ms=15000;
    req.body=std::string("\x78\x00",2);
    req.headers={
        {"Content-Type","application/x-protobuf"},
        {"Accept-Encoding","identity"},
        {"Accept","application/octet-stream"},
    };
    auto response=http_.perform(req);
    result.http_status=response.response.status_code;
    if(!response.ok) {
        result.failure=UmpTransferFailure::TransportRejected;
        return result;
    }
    if(result.http_status!=200) {
        result.failure=UmpTransferFailure::HttpRejected;
        return result;
    }
    if(response.response.body.size()>1024*1024+128*1024) {
        result.failure=UmpTransferFailure::InvalidEnvelope;
        return result;
    }
    for(const auto& h:response.response.headers) {
        if(h.name=="Content-Encoding" && h.value!="identity") {
            result.failure=UmpTransferFailure::InvalidEnvelope;
            return result;
        }
        // For UMP media envelopes the precise byte framing is the content
        // validator. Never accept a returned HTML error page as media.
        if(h.name=="Content-Type" && h.value.find("text/html")==0) {
            result.failure=UmpTransferFailure::InvalidEnvelope;
            return result;
        }
    }
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(
        response.response.body.data());
    const auto media=parse_bounded_ump_media(bytes,response.response.body.size());
    if(!media) {
        result.failure=UmpTransferFailure::MissingMedia;
        return result;
    }
    result.bytes=std::move(*media);
    result.failure=UmpTransferFailure::None;
    return result;
}
} // namespace ttnx::playback
