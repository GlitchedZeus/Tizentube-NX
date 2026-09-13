#pragma once

#include <string>
#include <vector>

namespace ttnx::net {

enum class HttpMethod {
    Get,
    Post,
};

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    HttpMethod method{HttpMethod::Get};
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;
    long timeout_ms{15000};
};

struct HttpResponse {
    long status_code{0};
    std::vector<HttpHeader> headers;
    std::string body;
};

struct HttpResult {
    bool ok{false};
    HttpResponse response;
    std::string error;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResult perform(const HttpRequest& request) = 0;
};

}  // namespace ttnx::net
