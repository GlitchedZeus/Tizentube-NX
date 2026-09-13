#include "libnx_http_client.hpp"

#include "tizentube_nx/net/http1.hpp"
#include "tizentube_nx/net/outbound_policy.hpp"

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

int tcp_connect_allowed_host(std::string_view host, long timeout_ms) {
    // SECOND SAFETY GATE: this helper is the only place that calls getaddrinfo().
    // Keep the allowlist check directly adjacent to DNS so future refactors
    // cannot accidentally resolve an unapproved/Nintendo endpoint.
    if (!net::is_allowed_outbound_host(host)) return -1;

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

bool read_all(SslConnection& connection, std::string& raw, std::string& error) {
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
        if (raw.size() > kMaxRawResponseBytes ||
            received > kMaxRawResponseBytes - raw.size()) {
            error = "HTTPS response exceeded the bounded raw-response limit.";
            return false;
        }
        raw.append(buffer, received);
    }
}

net::HttpResult perform_single_request(
    const net::HttpRequest& request,
    const net::HttpsUrl& parsed_url,
    long timeout_ms) {
    net::HttpResult result;

    const auto wire_request = net::build_http1_request(request, parsed_url);
    if (!wire_request) {
        result.error = "Could not serialize the bounded HTTP/1.1 request.";
        return result;
    }
    if (wire_request->size() > kMaxRequestBytes) {
        result.error = "HTTP request exceeds the bounded request limit.";
        return result;
    }

    TlsConnection tls;
    tls.raw_fd = tcp_connect_allowed_host(parsed_url.host, timeout_ms);
    if (tls.raw_fd < 0) {
        result.error = "TCP connection to the approved YouTube host failed.";
        return result;
    }

    u32 tls_versions = SslVersion_TlsV12;
    if (hosversionAtLeast(11, 0, 0)) tls_versions |= SslVersion_TlsV13;

    Result rc = sslCreateContext(&tls.context, tls_versions);
    if (R_FAILED(rc)) {
        result.error = result_error("sslCreateContext", rc);
        return result;
    }
    tls.have_context = true;

    rc = sslContextCreateConnection(&tls.context, &tls.connection);
    if (R_FAILED(rc)) {
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
        result.error = result_error("sslConnectionSetVerifyOption", rc);
        return result;
    }

    rc = sslConnectionSetHostName(
        &tls.connection,
        parsed_url.host.c_str(),
        static_cast<u32>(parsed_url.host.size()));
    if (R_FAILED(rc)) {
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
        result.error = "socketSslConnectionSetSocketDescriptor failed.";
        return result;
    }
    if (handed_off_fd >= 0) tls.ssl_fd = handed_off_fd;

    rc = sslConnectionSetIoMode(&tls.connection, SslIoMode_Blocking);
    if (R_FAILED(rc)) {
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
        result.error = result_error("sslConnectionDoHandshake", rc);
        return result;
    }

    if (!write_all(tls.connection, *wire_request, result.error)) return result;

    std::string raw_response;
    raw_response.reserve(64 * 1024);
    if (!read_all(tls.connection, raw_response, result.error)) return result;

    auto parsed_response = net::parse_http1_response(raw_response, kMaxResponseBodyBytes);
    if (!parsed_response) {
        result.error = "HTTP/1.1 response rejected: " + parsed_response.error;
        return result;
    }

    result.response = std::move(*parsed_response.response);
    result.ok = result.response.status_code >= 200 && result.response.status_code < 300;
    if (!result.ok) {
        result.error = "HTTP request returned status " +
                       std::to_string(result.response.status_code) + ".";
    }
    return result;
}

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
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        initialization_error_ = result_error("socketInitializeDefault", rc);
        return;
    }
    sockets_ready_ = true;

    rc = sslInitialize(3);
    if (R_FAILED(rc)) {
        initialization_error_ = result_error("sslInitialize", rc);
        socketExit();
        sockets_ready_ = false;
        return;
    }
    ssl_ready_ = true;
    ready_ = true;
}

LibnxHttpClient::~LibnxHttpClient() {
    if (ssl_ready_) sslExit();
    if (sockets_ready_) socketExit();
}

net::HttpResult LibnxHttpClient::perform(const net::HttpRequest& request) {
    net::HttpResult result;
    if (!ready_) {
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
            result.error = "Outbound destination blocked by TizenTube NX network policy.";
            return result;
        }

        const auto parsed_url = net::parse_https_url(current.url);
        if (!parsed_url || parsed_url->host != allowed->host || parsed_url->port != allowed->port) {
            result.error = "HTTPS URL rejected by the strict transport parser.";
            return result;
        }

        result = perform_single_request(current, *parsed_url, timeout_ms);
        const long status = result.response.status_code;
        if (status < 300 || status >= 400) return result;

        if (current.method != net::HttpMethod::Get) {
            result.ok = false;
            result.error = "Redirects for POST requests are rejected fail-closed.";
            return result;
        }
        if (redirect_index == kMaxRedirects) {
            result.ok = false;
            result.error = "HTTPS redirect limit exceeded.";
            return result;
        }

        const auto next_url = redirect_url(result.response, *parsed_url);
        if (!next_url) {
            result.ok = false;
            result.error = "HTTPS redirect was missing one safe absolute/root-relative Location.";
            return result;
        }

        // Validate the redirect immediately. The loop validates it again right
        // before the next DNS lookup, giving redirects two independent checks.
        if (!net::parse_allowed_https_destination(*next_url)) {
            result.ok = false;
            result.error = "HTTPS redirect blocked by TizenTube NX network policy.";
            return result;
        }

        current.url = *next_url;
        current.body.clear();
    }

    result.error = "Unexpected redirect state.";
    return result;
}

}  // namespace ttnx::switch_app
