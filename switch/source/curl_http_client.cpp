#include "curl_http_client.hpp"

#include "tizentube_nx/net/outbound_policy.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace ttnx::switch_app {
namespace {

size_t write_body(char* data, size_t size, size_t count, void* userdata) {
    const size_t bytes = size * count;
    auto* output = static_cast<std::string*>(userdata);
    output->append(data, bytes);
    return bytes;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

size_t write_header(char* data, size_t size, size_t count, void* userdata) {
    const size_t bytes = size * count;
    auto* headers = static_cast<std::vector<net::HttpHeader>*>(userdata);
    std::string_view line(data, bytes);
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return bytes;

    const auto name = trim(line.substr(0, colon));
    const auto value = trim(line.substr(colon + 1));
    if (!name.empty()) headers->push_back({std::string(name), std::string(value)});
    return bytes;
}

}  // namespace

CurlHttpClient::CurlHttpClient(std::string ca_bundle_path)
    : ca_bundle_path_(std::move(ca_bundle_path)) {
    const Result socket_result = socketInitializeDefault();
    if (R_FAILED(socket_result)) {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "socketInitializeDefault failed: 0x%08x",
                      static_cast<unsigned int>(socket_result));
        initialization_error_ = buffer;
        return;
    }
    sockets_ready_ = true;

    const CURLcode curl_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_result != CURLE_OK) {
        initialization_error_ = std::string("curl_global_init failed: ") +
                                curl_easy_strerror(curl_result);
        socketExit();
        sockets_ready_ = false;
        return;
    }
    curl_ready_ = true;
    ready_ = true;
}

CurlHttpClient::~CurlHttpClient() {
    if (curl_ready_) curl_global_cleanup();
    if (sockets_ready_) socketExit();
}

net::HttpResult CurlHttpClient::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    if (!ready_) {
        result.error = initialization_error_.empty()
            ? "HTTP client is not initialized."
            : initialization_error_;
        return result;
    }

    // CRITICAL CONSOLE-SAFETY BOUNDARY:
    // This check occurs before curl can perform DNS resolution or create a TLS
    // connection. The app is deny-by-default and must never rely on 90DNS to
    // keep Nintendo endpoints unreachable.
    const auto destination = net::parse_allowed_https_destination(request.url);
    if (!destination) {
        result.error = "Outbound destination blocked by TizenTube NX network policy.";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed.";
        return result;
    }

    curl_slist* header_list = nullptr;
    for (const auto& header : request.headers) {
        const std::string line = header.name + ": " + header.value;
        curl_slist* next = curl_slist_append(header_list, line.c_str());
        if (!next) {
            curl_slist_free_all(header_list);
            curl_easy_cleanup(curl);
            result.error = "Could not allocate HTTP headers.";
            return result;
        }
        header_list = next;
    }

    char error_buffer[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &result.response.headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeout_ms > 0 ? request.timeout_ms : 15000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     std::min(request.timeout_ms > 0 ? request.timeout_ms : 15000L, 7000L));

    // Never let libcurl follow a redirect autonomously. A redirect must be
    // surfaced to TizenTube NX and re-validated by the same host allowlist
    // before another DNS lookup is permitted.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (!ca_bundle_path_.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle_path_.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);

    if (request.method == net::HttpMethod::Post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    const CURLcode perform_result = curl_easy_perform(curl);
    if (perform_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.response.status_code);
        result.ok = result.response.status_code >= 200 && result.response.status_code < 300;
        if (!result.ok) {
            result.error = "HTTP request failed with status " +
                           std::to_string(result.response.status_code) + ".";
        }
    } else {
        result.error = error_buffer[0] != '\0'
            ? std::string(error_buffer)
            : std::string(curl_easy_strerror(perform_result));
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return result;
}

}  // namespace ttnx::switch_app
