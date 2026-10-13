#include "libnx_http_client.hpp"

#include "tizentube_nx/net/http1.hpp"
#include "tizentube_nx/net/http1_framing.hpp"
#include "tizentube_nx/net/outbound_policy.hpp"
#ifdef TTNX_M3_MEDIA_PLANE
#include "tizentube_nx/playback/playback.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
#include "tizentube_nx/playback/source_routes.hpp"
#ifdef TTNX_M4_ROLLING_STREAM
#include "tizentube_nx/net/http1_media_stream.hpp"
#endif
#endif

#include <switch.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

namespace ttnx::switch_app {
namespace {

constexpr std::size_t kMaxRequestBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxResponseBodyBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxRawResponseBytes = kMaxResponseBodyBytes + 1024 * 1024;
constexpr int kMaxRedirects = 3;
constexpr long kDefaultTimeoutMs = 15000;
constexpr long kMaxTimeoutMs = 30000;
constexpr long kConnectTimeoutCapMs = 7000;

std::string result_error(std::string_view operation, Result rc) {
    char code[16]{};
    std::snprintf(code, sizeof(code), "0x%08x", static_cast<unsigned int>(rc));
    return std::string(operation) + " failed: " + code;
}

long normalized_timeout(long requested) {
    if (requested <= 0) return kDefaultTimeoutMs;
    return std::clamp(requested, 1L, kMaxTimeoutMs);
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::size_t header_count(const net::HttpResponse& response, std::string_view name) {
    std::size_t count = 0;
    for (const auto& header : response.headers) {
        if (iequals(header.name, name)) ++count;
    }
    return count;
}

struct TlsConnection {
    int raw_fd{-1};
    int ssl_fd{-1};
    SslContext context{};
    SslConnection connection{};
    bool have_context{false};
    bool have_connection{false};

    ~TlsConnection() {
        // The fd returned by socketSslConnectionSetSocketDescriptor belongs to
        // the caller and must be closed before sslConnectionClose().
        if (ssl_fd >= 0) {
            close(ssl_fd);
            ssl_fd = -1;
        }
        if (have_connection) {
            sslConnectionClose(&connection);
            have_connection = false;
        }
        if (have_context) {
            sslContextClose(&context);
            have_context = false;
        }
        // This is only still owned here if socket handoff never occurred.
        if (raw_fd >= 0) {
            close(raw_fd);
            raw_fd = -1;
        }
    }
};

void set_socket_timeouts(int fd, long timeout_ms) {
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int connect_one_with_timeout(const addrinfo& address, long timeout_ms) {
    const int fd = socket(address.ai_family, address.ai_socktype, address.ai_protocol);
    if (fd < 0) return -1;

    const int old_flags = fcntl(fd, F_GETFL, 0);
    if (old_flags < 0 || fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    int connect_result = connect(fd, address.ai_addr, address.ai_addrlen);
    if (connect_result < 0 && errno == EINPROGRESS) {
        pollfd poll_state{};
        poll_state.fd = fd;
        poll_state.events = POLLOUT;
        const int poll_result = poll(&poll_state, 1, static_cast<int>(timeout_ms));
        if (poll_result > 0 && (poll_state.revents & (POLLOUT | POLLERR | POLLHUP))) {
            int socket_error = 0;
            socklen_t socket_error_size = sizeof(socket_error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) == 0 &&
                socket_error == 0) {
                connect_result = 0;
            }
        }
    }

    if (connect_result != 0) {
        close(fd);
        return -1;
    }

    if (fcntl(fd, F_SETFL, old_flags) < 0) {
        close(fd);
        return -1;
    }
    set_socket_timeouts(fd, timeout_ms);
    return fd;
}

int tcp_connect_allowed_host(std::string_view host, long timeout_ms,
                             bool media_plane = false) {
    // SECOND SAFETY GATE: browser traffic is still exactly www.youtube.com.
    // The opt-in media variant uses only approved signed video-CDN hosts.
    if (media_plane) {
#ifdef TTNX_M3_MEDIA_PLANE
        const std::string proof_url = "https://" + std::string(host) + "/videoplayback";
        if (!playback::is_reviewable_googlevideo_https_url(proof_url)) return -1;
#else
        return -1;
#endif
    } else if (!net::is_allowed_outbound_host(host)) return -1;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // Protocol 0 lets getaddrinfo choose the protocol for a SOCK_STREAM socket;
    // on the Switch this resolves to TCP without depending on IPPROTO_TCP being
    // exposed by the devkitA64 libc headers.
    hints.ai_protocol = 0;

    addrinfo* results = nullptr;
    const std::string host_string(host);
    const int resolve_result = getaddrinfo(host_string.c_str(), "443", &hints, &results);
    if (resolve_result != 0 || !results) return -1;

    const long connect_timeout = std::min(timeout_ms, kConnectTimeoutCapMs);
    int fd = -1;
    for (addrinfo* current = results; current; current = current->ai_next) {
        fd = connect_one_with_timeout(*current, connect_timeout);
        if (fd >= 0) break;
    }
    freeaddrinfo(results);
    return fd;
}

bool write_all(SslConnection& connection, std::string_view data, std::string& error) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto remaining = data.size() - offset;
        const auto chunk = static_cast<u32>(std::min<std::size_t>(
            remaining, std::numeric_limits<u32>::max()));
        u32 written = 0;
        const Result rc = sslConnectionWrite(
            &connection, data.data() + offset, chunk, &written);
        if (R_FAILED(rc)) {
            error = result_error("sslConnectionWrite", rc);
            return false;
        }
        if (written == 0 || written > chunk) {
            error = "sslConnectionWrite made no forward progress.";
            return false;
        }
        offset += written;
    }
    return true;
}

bool read_all(SslConnection& connection, std::string& raw, std::string& error,
              std::size_t cap=kMaxRawResponseBytes) {
    char buffer[32 * 1024];
    while (true) {
        u32 received = 0;
        const Result rc = sslConnectionRead(
            &connection, buffer, static_cast<u32>(sizeof(buffer)), &received);
        if (R_FAILED(rc)) {
            error = result_error("sslConnectionRead", rc);
            return false;
        }
        if (received == 0) return true;
        if (raw.size() > cap ||
            received > cap - raw.size()) {
            error = "HTTPS response exceeded the bounded raw-response limit.";
            return false;
        }
        raw.append(buffer, received);
        const auto frame=net::inspect_http1_frame(raw,cap);
        if(frame.kind==net::HttpFrameKind::Rejected) {
            error="HTTPS response framing was invalid or exceeded bounds.";
            return false;
        }
        if(frame.kind==net::HttpFrameKind::FixedLength) {
            if(raw.size()>frame.total) {
                error="HTTPS response exceeded declared Content-Length.";
                return false;
            }
            if(raw.size()==frame.total)return true;
        }
    }
}

net::HttpResult perform_single_request(
    const net::HttpRequest& request,
    const net::HttpsUrl& parsed_url,
    long timeout_ms,
    bool media_plane = false,
    std::size_t response_cap=kMaxRawResponseBytes) {
    net::HttpResult result;

    const auto wire_request = net::build_http1_request(request, parsed_url);
    if (!wire_request) {
        result.failure_stage = net::HttpFailureStage::Http;
        result.error = "Could not serialize the bounded HTTP/1.1 request.";
        return result;
    }
    if (wire_request->size() > kMaxRequestBytes) {
        result.failure_stage = net::HttpFailureStage::Http;
        result.error = "HTTP request exceeds the bounded request limit.";
        return result;
    }

    TlsConnection tls;
    tls.raw_fd = tcp_connect_allowed_host(parsed_url.host, timeout_ms, media_plane);
    if (tls.raw_fd < 0) {
        result.failure_stage = net::HttpFailureStage::Tcp;
        result.error = "TCP connection to the approved YouTube host failed.";
        return result;
    }

    u32 tls_versions = SslVersion_TlsV12;
    if (hosversionAtLeast(11, 0, 0)) tls_versions |= SslVersion_TlsV13;

    Result rc = sslCreateContext(&tls.context, tls_versions);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslCreateContext", rc);
        return result;
    }
    tls.have_context = true;

    rc = sslContextCreateConnection(&tls.context, &tls.connection);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslContextCreateConnection", rc);
        return result;
    }
    tls.have_connection = true;

    // Explicitly require peer-chain, hostname and certificate-date validation.
    // HostName verification also requires SetHostName, which supplies SNI.
    rc = sslConnectionSetVerifyOption(
        &tls.connection,
        SslVerifyOption_PeerCa | SslVerifyOption_HostName | SslVerifyOption_DateCheck);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionSetVerifyOption", rc);
        return result;
    }

    rc = sslConnectionSetHostName(
        &tls.connection,
        parsed_url.host.c_str(),
        static_cast<u32>(parsed_url.host.size()));
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionSetHostName", rc);
        return result;
    }

    errno = 0;
    const int handed_off_fd = socketSslConnectionSetSocketDescriptor(
        &tls.connection, tls.raw_fd);
    // libnx's wrapper consumes the input descriptor. The returned descriptor,
    // when present, is the descriptor we must close before the SSL connection.
    tls.raw_fd = -1;
    if (handed_off_fd < 0 && errno != ENOENT) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = "socketSslConnectionSetSocketDescriptor failed.";
        return result;
    }
    if (handed_off_fd >= 0) tls.ssl_fd = handed_off_fd;

    rc = sslConnectionSetIoMode(&tls.connection, SslIoMode_Blocking);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionSetIoMode", rc);
        return result;
    }

    // On HOS 16+, tighten the SSL service's own I/O timeout in addition to the
    // BSD socket timeouts.
    if (hosversionAtLeast(16, 0, 0)) {
        rc = sslConnectionSetIoTimeout(
            &tls.connection,
            static_cast<u32>(std::min<long>(timeout_ms, std::numeric_limits<u32>::max())));
        if (R_FAILED(rc)) {
            result.failure_stage = net::HttpFailureStage::Tls;
            result.error = result_error("sslConnectionSetIoTimeout", rc);
            return result;
        }
    }

    u32 certificate_bytes = 0;
    u32 certificate_count = 0;
    rc = sslConnectionDoHandshake(
        &tls.connection,
        &certificate_bytes,
        &certificate_count,
        nullptr,
        0);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionDoHandshake", rc);
        return result;
    }

    if (!write_all(tls.connection, *wire_request, result.error)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        return result;
    }

    std::string raw_response;
    raw_response.reserve(64 * 1024);
    if (!read_all(tls.connection, raw_response, result.error, response_cap)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        return result;
    }

    auto parsed_response = net::parse_http1_response(raw_response, kMaxResponseBodyBytes);
    if (!parsed_response) {
        result.failure_stage = net::HttpFailureStage::Http;
        result.error = "HTTP/1.1 response rejected: " + parsed_response.error;
        return result;
    }

    result.response = std::move(*parsed_response.response);
    result.ok = result.response.status_code >= 200 && result.response.status_code < 300;
    if (!result.ok) {
        result.failure_stage = net::HttpFailureStage::Http;
        result.error = "HTTP request returned status " +
                       std::to_string(result.response.status_code) + ".";
    }
    return result;
}


#if defined(TTNX_M3_MEDIA_PLANE) && defined(TTNX_M4_ROLLING_STREAM)
// Media-only opt-in direct Horizon SSL read to app-owned SD chunks.
// Keep the accepted perform_single_request() byte-for-byte unchanged.
net::HttpResult perform_single_streamed_media_request(
    const net::HttpRequest& request,const net::HttpsUrl& parsed_url,
    long timeout_ms,const playback::HlsMediaChunk& emit) {
    net::HttpResult result;
    const auto wire_request = net::build_http1_request(request, parsed_url);
    if (!wire_request) {
        result.failure_stage = net::HttpFailureStage::Http;
        result.error = "Could not serialize the bounded HTTP/1.1 request.";
        return result;
    }
    if (wire_request->size() > kMaxRequestBytes) {
        result.failure_stage = net::HttpFailureStage::Http;
        result.error = "HTTP request exceeds the bounded request limit.";
        return result;
    }

    TlsConnection tls;
    tls.raw_fd = tcp_connect_allowed_host(parsed_url.host, timeout_ms, media_plane);
    if (tls.raw_fd < 0) {
        result.failure_stage = net::HttpFailureStage::Tcp;
        result.error = "TCP connection to the approved YouTube host failed.";
        return result;
    }

    u32 tls_versions = SslVersion_TlsV12;
    if (hosversionAtLeast(11, 0, 0)) tls_versions |= SslVersion_TlsV13;

    Result rc = sslCreateContext(&tls.context, tls_versions);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslCreateContext", rc);
        return result;
    }
    tls.have_context = true;

    rc = sslContextCreateConnection(&tls.context, &tls.connection);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslContextCreateConnection", rc);
        return result;
    }
    tls.have_connection = true;

    // Explicitly require peer-chain, hostname and certificate-date validation.
    // HostName verification also requires SetHostName, which supplies SNI.
    rc = sslConnectionSetVerifyOption(
        &tls.connection,
        SslVerifyOption_PeerCa | SslVerifyOption_HostName | SslVerifyOption_DateCheck);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionSetVerifyOption", rc);
        return result;
    }

    rc = sslConnectionSetHostName(
        &tls.connection,
        parsed_url.host.c_str(),
        static_cast<u32>(parsed_url.host.size()));
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionSetHostName", rc);
        return result;
    }

    errno = 0;
    const int handed_off_fd = socketSslConnectionSetSocketDescriptor(
        &tls.connection, tls.raw_fd);
    // libnx's wrapper consumes the input descriptor. The returned descriptor,
    // when present, is the descriptor we must close before the SSL connection.
    tls.raw_fd = -1;
    if (handed_off_fd < 0 && errno != ENOENT) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = "socketSslConnectionSetSocketDescriptor failed.";
        return result;
    }
    if (handed_off_fd >= 0) tls.ssl_fd = handed_off_fd;

    rc = sslConnectionSetIoMode(&tls.connection, SslIoMode_Blocking);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionSetIoMode", rc);
        return result;
    }

    // On HOS 16+, tighten the SSL service's own I/O timeout in addition to the
    // BSD socket timeouts.
    if (hosversionAtLeast(16, 0, 0)) {
        rc = sslConnectionSetIoTimeout(
            &tls.connection,
            static_cast<u32>(std::min<long>(timeout_ms, std::numeric_limits<u32>::max())));
        if (R_FAILED(rc)) {
            result.failure_stage = net::HttpFailureStage::Tls;
            result.error = result_error("sslConnectionSetIoTimeout", rc);
            return result;
        }
    }

    u32 certificate_bytes = 0;
    u32 certificate_count = 0;
    rc = sslConnectionDoHandshake(
        &tls.connection,
        &certificate_bytes,
        &certificate_count,
        nullptr,
        0);
    if (R_FAILED(rc)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = result_error("sslConnectionDoHandshake", rc);
        return result;
    }

    if (!write_all(tls.connection, *wire_request, result.error)) {
        result.failure_stage = net::HttpFailureStage::Tls;
        return result;
    }


    net::Http1MediaStream parser;
    char receive[net::Http1MediaStream::kReceiveChunk];
    for(;;) {
        u32 received=0;
        const Result rc=sslConnectionRead(
            &tls.connection,receive,static_cast<u32>(sizeof(receive)),&received);
        if(R_FAILED(rc)) {
            result.failure_stage=net::HttpFailureStage::Tls;
            result.error="M4 media SSL receive failure.";
            return result;
        }
        if(received==0) {
            if(!parser.finish()) {
                result.failure_stage=net::HttpFailureStage::Http;
                result.error="M4 media stream truncated or incomplete.";
                return result;
            }
            result.ok=true;result.response.status_code=200;
            return result;
        }
        if(!parser.feed(std::string_view(receive,received),emit)) {
            result.failure_stage=net::HttpFailureStage::Http;
            result.error="M4 media HTTP framing, size or SD emission refused.";
            return result;
        }
        if(parser.state()==net::Http1MediaStream::State::Body &&
           parser.received()==parser.expected()) {
            if(!parser.finish()) {
                result.failure_stage=net::HttpFailureStage::Http;
                result.error="M4 media stream finish refused.";
                return result;
            }
            result.ok=true;result.response.status_code=200;
            return result; // Content-Length complete; no need to wait for EOF.
        }
    }
}
#endif

std::optional<std::string> redirect_url(
    const net::HttpResponse& response,
    const net::HttpsUrl& current_url) {
    if (header_count(response, "Location") != 1) return std::nullopt;
    const auto location = net::find_header_value(response, "Location");
    if (!location || location->empty()) return std::nullopt;

    if (location->starts_with("https://") || location->starts_with("HTTPS://")) {
        return std::string(*location);
    }
    // Network-path references (//host/path) are deliberately rejected rather
    // than interpreted, because they can change authority.
    if (location->starts_with("//")) return std::nullopt;
    if (location->front() == '/') {
        return "https://" + current_url.host + std::string(*location);
    }
    // Fail closed on other relative forms for the first networking milestone.
    return std::nullopt;
}

}  // namespace

LibnxHttpClient::LibnxHttpClient() {
    const Result socket_rc = socketInitializeDefault();
    const Result already_initialized =
        MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    const auto socket_init = net::classify_non_refcounted_service_init(
        static_cast<std::uint32_t>(socket_rc),
        static_cast<std::uint32_t>(already_initialized));
    socket_ownership_ = socket_init.ownership;
    if (!socket_init.usable()) {
        initialization_failure_ = NativeInitFailure::Socket;
        initialization_error_ = result_error("socketInitializeDefault", socket_rc);
        return;
    }

    // sslInitialize() is backed by libnx ServiceGuard. Successful calls acquire
    // a reference even if ssl is already initialized elsewhere; each success
    // therefore owns exactly one matching sslExit(). Unlike socket init, a
    // non-zero AlreadyInitialized result is not treated as borrowable here.
    const Result ssl_rc = sslInitialize(3);
    const auto ssl_init = net::classify_refcounted_service_init(
        static_cast<std::uint32_t>(ssl_rc));
    ssl_ownership_ = ssl_init.ownership;
    if (!ssl_init.usable()) {
        initialization_failure_ = NativeInitFailure::Ssl;
        initialization_error_ = result_error("sslInitialize", ssl_rc);
        if (socket_init.cleanup_required()) {
            socketExit();
            socket_ownership_ = net::ServiceOwnership::Unavailable;
        }
        return;
    }

    ready_ = true;
}

LibnxHttpClient::~LibnxHttpClient() {
    if (ssl_ownership_ == net::ServiceOwnership::Owned) {
        sslExit();
        ssl_ownership_ = net::ServiceOwnership::Unavailable;
    }
    if (socket_ownership_ == net::ServiceOwnership::Owned) {
        socketExit();
        socket_ownership_ = net::ServiceOwnership::Unavailable;
    }
}

std::string LibnxHttpClient::service_status() const {
    std::string status = "Sockets: ";
    switch (socket_ownership_) {
        case net::ServiceOwnership::Owned:
            status += "owned";
            break;
        case net::ServiceOwnership::Borrowed:
            status += "existing";
            break;
        case net::ServiceOwnership::Unavailable:
            status += "unavailable";
            break;
    }

    status += " | SSL: ";
    switch (ssl_ownership_) {
        case net::ServiceOwnership::Owned:
            status += "ready";
            break;
        case net::ServiceOwnership::Borrowed:
            status += "existing";
            break;
        case net::ServiceOwnership::Unavailable:
            status += "unavailable";
            break;
    }
    return status;
}

net::HttpResult LibnxHttpClient::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    if (!ready_) {
        if (initialization_failure_ == NativeInitFailure::Ssl) {
            result.failure_stage = net::HttpFailureStage::Tls;
        } else if (initialization_failure_ == NativeInitFailure::Socket) {
            result.failure_stage = net::HttpFailureStage::Tcp;
        }
        result.error = initialization_error_.empty()
            ? "Native libnx HTTPS client is not initialized."
            : initialization_error_;
        return result;
    }

    net::HttpRequest current = request;
    const long timeout_ms = normalized_timeout(request.timeout_ms);

    for (int redirect_index = 0; redirect_index <= kMaxRedirects; ++redirect_index) {
        // PRIMARY CONSOLE-SAFETY GATE. This runs before parse_https_url(),
        // getaddrinfo(), socket(), TLS or HTTP. 90DNS is not part of this trust
        // decision and Nintendo destinations are never authorized here.
        const auto allowed = net::parse_allowed_https_destination(current.url);
        if (!allowed) {
            result.failure_stage = net::HttpFailureStage::Policy;
            result.error = "Outbound destination blocked by TizenTube NX network policy.";
            return result;
        }

        const auto parsed_url = net::parse_https_url(current.url);
        if (!parsed_url || parsed_url->host != allowed->host || parsed_url->port != allowed->port) {
            result.failure_stage = net::HttpFailureStage::Policy;
            result.error = "HTTPS URL rejected by the strict transport parser.";
            return result;
        }

        result = perform_single_request(current, *parsed_url, timeout_ms);
        const long status = result.response.status_code;
        if (status < 300 || status >= 400) return result;

        if (current.method != net::HttpMethod::Get) {
            result.ok = false;
            result.failure_stage = net::HttpFailureStage::Http;
            result.error = "Redirects for POST requests are rejected fail-closed.";
            return result;
        }
        if (redirect_index == kMaxRedirects) {
            result.ok = false;
            result.failure_stage = net::HttpFailureStage::Http;
            result.error = "HTTPS redirect limit exceeded.";
            return result;
        }

        const auto next_url = redirect_url(result.response, *parsed_url);
        if (!next_url) {
            result.ok = false;
            result.failure_stage = net::HttpFailureStage::Http;
            result.error = "HTTPS redirect was missing one safe absolute/root-relative Location.";
            return result;
        }

        // Validate the redirect immediately. The loop validates it again right
        // before the next DNS lookup, giving redirects two independent checks.
        if (!net::parse_allowed_https_destination(*next_url)) {
            result.ok = false;
            result.failure_stage = net::HttpFailureStage::Policy;
            result.error = "HTTPS redirect blocked by TizenTube NX network policy.";
            return result;
        }

        current.url = *next_url;
        current.body.clear();
    }

    result.failure_stage = net::HttpFailureStage::Http;
    result.error = "Unexpected redirect state.";
    return result;
}


#ifdef TTNX_M3_MEDIA_PLANE
net::HttpResult LibnxMediaChunkTransport::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    // Dedicated M3 media-only transport. No arbitrary headers, credentials,
    // redirects, manifests, or browser data; only strict bounded GET ranges.
    if (!services_.ready()) {
        result.failure_stage = net::HttpFailureStage::Tls;
        result.error = "M3 media SSL/socket services unavailable.";
        return result;
    }
    if (request.method != net::HttpMethod::Get || !request.body.empty() ||
        request.headers.size() != 3 ||
        request.headers[0].name != "Range" ||
        !request.headers[0].value.starts_with("bytes=") ||
        request.headers[1].name != "Accept-Encoding" ||
        request.headers[1].value != "identity" ||
        request.headers[2].name != "Accept" ||
        request.headers[2].value != "application/octet-stream" ||
        !playback::is_reviewable_googlevideo_https_url(request.url)) {
        result.failure_stage = net::HttpFailureStage::Policy;
        result.error = "M3 media request rejected before network access.";
        return result;
    }
    const auto parsed = net::parse_https_url(request.url);
    if (!parsed || parsed->port != 443 ||
        !playback::is_reviewable_googlevideo_https_url(
            "https://" + parsed->host + parsed->target)) {
        result.failure_stage = net::HttpFailureStage::Policy;
        result.error = "M3 media host rejected by strict URL parser.";
        return result;
    }
    // Each call is a single TLS request and repeats pre-DNS media host
    // checks, hostname/CA verification and response size caps. Never auto-
    // follow Location here: MediaChunkReader decides whether the next
    // approved request is permissible and preserves the original Range.
    return perform_single_request(request, *parsed,
                                  normalized_timeout(request.timeout_ms),
                                  true);
}

net::HttpResult LibnxUmpChunkTransport::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    auto reject = [&](const char* message) {
        result.ok = false;
        result.failure_stage = net::HttpFailureStage::Policy;
        result.error = message; // constant text: never include signed URLs
        return result;
    };
    if (!services_.ready()) return reject("Experimental UMP transport offline.");
    if (request.method != net::HttpMethod::Post ||
        request.body.size() != 2 ||
        static_cast<unsigned char>(request.body[0]) != 0x78 ||
        request.body[1] != '\0' ||
        request.headers.size() != 3 ||
        request.headers[0].name != "Content-Type" ||
        request.headers[0].value != "application/x-protobuf" ||
        request.headers[1].name != "Accept-Encoding" ||
        request.headers[1].value != "identity" ||
        request.headers[2].name != "Accept" ||
        request.headers[2].value != "application/octet-stream" ||
        !playback::is_reviewable_googlevideo_https_url(request.url))
        return reject("UMP request shape or source rejected before DNS.");

    // Confirm the original bounded media range and protocol parameters,
    // rather than letting arbitrary callers send POSTs through this socket.
    auto read_param = [&](std::string_view name) -> std::optional<std::string> {
        const auto q = request.url.find('?');
        if (q == std::string::npos) return std::nullopt;
        std::string_view params(request.url.data()+q+1,
                                request.url.size()-q-1);
        std::optional<std::string> value;
        while (!params.empty()) {
            const auto amp = params.find('&');
            const auto item = params.substr(0,amp);
            const auto eq = item.find('=');
            if (eq != std::string_view::npos && item.substr(0,eq)==name) {
                if (value) return std::nullopt; // duplicate parameter
                value=std::string(item.substr(eq+1));
            }
            if (amp == std::string_view::npos) break;
            params.remove_prefix(amp+1);
        }
        return value;
    };
    const auto range=read_param("range");
    if (read_param("ump")!=std::optional<std::string>{"1"} ||
        read_param("srfvp")!=std::optional<std::string>{"1"} ||
        read_param("alr")!=std::optional<std::string>{"yes"} ||
        !read_param("rn") || !range)
        return reject("UMP required protocol markers missing or duplicated.");
    const auto dash=range->find('-');
    if (dash==std::string::npos || dash==0 || dash+1>=range->size())
        return reject("UMP byte range malformed.");
    auto parse_u64=[](std::string_view digits,std::uint64_t& n) {
        if (digits.empty() || digits.size()>20) return false;
        n=0;
        for(const unsigned char c:digits) {
            if(c<'0'||c>'9')return false;
            const auto d=static_cast<unsigned>(c-'0');
            if(n>(UINT64_MAX-d)/10)return false;
            n=n*10+d;
        }
        return true;
    };
    std::uint64_t begin=0,end=0,sequence=0;
    if (!parse_u64(std::string_view(*range).substr(0,dash),begin) ||
        !parse_u64(std::string_view(*range).substr(dash+1),end) ||
        !parse_u64(*read_param("rn"),sequence) ||
        end<begin || end-begin>=1024*1024 ||
        end>=512ULL*1024*1024 || sequence==0 || sequence>4096)
        return reject("UMP range exceeds reviewed byte and resource limits.");

    const auto parsed=net::parse_https_url(request.url);
    if (!parsed || parsed->port!=443 ||
        !playback::is_reviewable_googlevideo_https_url(
            "https://"+parsed->host+parsed->target))
        return reject("UMP target rejected by strict HTTPS parser.");
    result=perform_single_request(request,*parsed,
                                  normalized_timeout(request.timeout_ms),true);
    // UMP POST requires HTTP 200; never follow Location (or SABR redirects).
    // A separate checked UMP wrapper parses MEDIA, redirect and protection.
    if (result.response.status_code >=300 && result.response.status_code<400)
        return reject("UMP redirected; this transport never follows redirects.");
    if (result.response.body.size()>1024*1024+128*1024)
        return reject("UMP envelope exceeds maximum bounded response size.");
    return result;
}

net::HttpResult LibnxHlsPlaylistTransport::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    const auto reject = [&](const char* message) {
        result.ok = false;
        result.failure_stage = net::HttpFailureStage::Policy;
        result.error = message;
        return result;
    };
    if (!services_.ready())return reject("M3 native HLS socket/TLS unavailable.");
    // No authorization/cookies, arbitrary method, request body, caller-chosen
    // headers or redirects. This does not grant libmpv direct network access.
    if (request.method!=net::HttpMethod::Get ||
        !request.body.empty() ||
        request.headers.size()!=2 ||
        request.headers[0].name!="Accept" ||
        request.headers[0].value!="application/vnd.apple.mpegurl" ||
        request.headers[1].name!="Accept-Encoding" ||
        request.headers[1].value!="identity" ||
        !(playback::is_reviewable_hls_manifest_url(request.url) ||
          playback::is_reviewable_hls_child_url(request.url)))
        return reject("M3 HLS path or request headers not reviewed.");
    const auto parsed=net::parse_https_url(request.url);
    if(!parsed || parsed->port!=443 ||
       !(playback::is_reviewable_hls_manifest_url(
             "https://"+parsed->host+parsed->target) ||
         playback::is_reviewable_hls_child_url(
             "https://"+parsed->host+parsed->target)))
        return reject("M3 HLS host rejected by strict HTTPS parser.");
    // Media-plane host check repeats right at tcp_connect_allowed_host,
    // before getaddrinfo(). Limit the RAW socket read to 1 MiB + 64 KiB
    // headers, not the browsing client's 17 MiB limit.
    result=perform_single_request(request,*parsed,
                                  normalized_timeout(request.timeout_ms),
                                  true,1024*1024+64*1024);
    if(result.response.status_code>=300 &&
       result.response.status_code<400)
        return reject("M3 HLS server redirect refused.");
    if(result.response.body.size()>1024*1024)
        return reject("M3 HLS playlist exceeded 1 MiB.");
    return result;
}

net::HttpResult LibnxHlsSegmentTransport::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    const auto reject = [&](const char* message) {
        result.ok=false;
        result.failure_stage=net::HttpFailureStage::Policy;
        result.error=message;
        return result;
    };
    if(!services_.ready())return reject("M3 HLS segment SSL not initialized.");
    if(request.method!=net::HttpMethod::Get || !request.body.empty() ||
       request.headers.size()!=2 ||
       request.headers[0].name!="Accept" ||
       request.headers[0].value!="application/octet-stream" ||
       request.headers[1].name!="Accept-Encoding" ||
       request.headers[1].value!="identity" ||
       !playback::is_reviewable_hls_child_url(request.url))
        return reject("M3 HLS segment request rejected before DNS.");

    const auto parsed=net::parse_https_url(request.url);
    if(!parsed || parsed->port!=443 ||
       !playback::is_reviewable_hls_child_url(
           "https://"+parsed->host+parsed->target))
        return reject("M3 HLS segment HTTPS destination rejected.");
    // Media-host allowlist rechecked immediately before getaddrinfo.
    // 16MiB segment cap + 64KiB envelope/header overhead prevents
    // malicious peer memory exhaustion.
    result=perform_single_request(request,*parsed,
                                  normalized_timeout(request.timeout_ms),
                                  true,16*1024*1024+64*1024);
    if(result.response.status_code>=300 &&
       result.response.status_code<400)
        return reject("M3 HLS segment redirected unexpectedly.");
    if(result.response.body.size()>16*1024*1024)
        return reject("M3 HLS segment exceeded bounded body.");
    return result;
}

#ifdef TTNX_M4_ROLLING_STREAM
playback::HlsStreamResult LibnxHlsSegmentTransport::stream_segment(
    std::string_view url,const playback::HlsMediaChunk& emit) {
    // Run only from the M4 worker thread; never the Borealis render tick.
    // Strict signed-CDN allowlist at call site AND pre-DNS in the helper.
    if(!services_.ready() || !emit ||
       !playback::is_reviewable_hls_child_url(url))return {};
    const auto parsed=net::parse_https_url(url);
    if(!parsed || parsed->port!=443 ||
       !playback::is_reviewable_hls_child_url(
           "https://"+parsed->host+parsed->target))return {};
    net::HttpRequest request;
    request.method=net::HttpMethod::Get;
    request.url=std::string(url);
    request.timeout_ms=20000;
    request.headers={{"Accept","application/octet-stream"},
                     {"Accept-Encoding","identity"}};
    std::size_t byte_count=0;
    const auto result=perform_single_streamed_media_request(
        request,*parsed,normalized_timeout(request.timeout_ms),
        [&](std::string_view bytes) {
            if(bytes.size()>32*1024 || !emit(bytes))return false;
            byte_count+=bytes.size();
            return true;
        });
    if(!result.ok || result.response.status_code!=200 ||
       byte_count<376 || byte_count>16*1024*1024)return {};
    return {true,byte_count};
}
#endif // M4 streaming adapter

#endif

}  // namespace ttnx::switch_app