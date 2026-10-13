#pragma once

#include "tizentube_nx/net/http.hpp"
#include "tizentube_nx/net/service_init.hpp"
#ifdef TTNX_M3_MEDIA_PLANE
#include "tizentube_nx/playback/media_chunks.hpp"
#include "tizentube_nx/playback/ump_transfer.hpp"
#include "tizentube_nx/playback/hls_manifest.hpp"
#include "tizentube_nx/playback/hls_segment.hpp"

#endif

#include <string>

namespace ttnx::switch_app {

enum class NativeInitFailure {
    None,
    Socket,
    Ssl,
};

// Native Switch HTTPS client backed by libnx BSD sockets + Horizon's local
// ssl service. All remote destinations are checked by the TizenTube NX
// default-deny outbound policy before DNS resolution.
class LibnxHttpClient final : public net::HttpClient {
public:
    LibnxHttpClient();
    ~LibnxHttpClient() override;

    LibnxHttpClient(const LibnxHttpClient&) = delete;
    LibnxHttpClient& operator=(const LibnxHttpClient&) = delete;

    net::HttpResult perform(const net::HttpRequest& request) override;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] NativeInitFailure initialization_failure() const noexcept {
        return initialization_failure_;
    }
    [[nodiscard]] net::ServiceOwnership socket_ownership() const noexcept {
        return socket_ownership_;
    }
    [[nodiscard]] net::ServiceOwnership ssl_ownership() const noexcept {
        return ssl_ownership_;
    }
    [[nodiscard]] const std::string& initialization_error() const noexcept {
        return initialization_error_;
    }
    [[nodiscard]] std::string service_status() const;

private:
    net::ServiceOwnership socket_ownership_{net::ServiceOwnership::Unavailable};
    net::ServiceOwnership ssl_ownership_{net::ServiceOwnership::Unavailable};
    NativeInitFailure initialization_failure_{NativeInitFailure::None};
    bool ready_{false};
    std::string initialization_error_;
};


#ifdef TTNX_M3_MEDIA_PLANE
// Distinct, experimental M3-only HTTP byte-range transport. Never enabled in
// the accepted M2 NRO, and never passed to guest Search/Home/Channel requests.
// Repeats a strict video-CDN host check immediately before DNS; no redirects.
class LibnxMediaChunkTransport final : public playback::MediaChunkTransport {
public:
    LibnxMediaChunkTransport() = default;
    net::HttpResult perform(const net::HttpRequest& request) override;
    [[nodiscard]] bool ready() const noexcept { return services_.ready(); }
private:
    LibnxHttpClient services_; // owns/borrows socket + Horizon SSL lifetimes
};

// Opt-in only. This POST transport repeats the same Googlevideo pre-DNS
// allowlist and rejects any request other than a fixed two-byte UMP body
// and a bounded one-MiB media range. Never routed into M2 guest browsing.
class LibnxUmpChunkTransport final : public net::HttpClient {
public:
    LibnxUmpChunkTransport() = default;
    net::HttpResult perform(const net::HttpRequest& request) override;
    [[nodiscard]] bool ready() const noexcept { return services_.ready(); }
private:
    LibnxHttpClient services_;
};
// Experimental playlist-only media HTTPS adapter. No redirects, no segment
// transfer, no web-player browser fallback and no enabled default-NRO access.
class LibnxHlsPlaylistTransport final : public net::HttpClient {
public:
    LibnxHlsPlaylistTransport() = default;
    net::HttpResult perform(const net::HttpRequest& request) override;
    [[nodiscard]] bool ready() const noexcept { return services_.ready(); }
private:
    LibnxHttpClient services_;
};

// Segment bytes require a DIFFERENT fixed MIME/size gate from text playlists.
// No user-supplied cookies, redirects, cross-origin requests or media GET in M2.
class LibnxHlsSegmentTransport final : public net::HttpClient,
                                       public playback::HlsSegmentStreamTransport {
public:
    LibnxHlsSegmentTransport() = default;
    net::HttpResult perform(const net::HttpRequest& request) override;
    [[nodiscard]] playback::HlsStreamResult stream_segment(
        std::string_view url,const playback::HlsMediaChunk& emit) override;
    [[nodiscard]] bool ready() const noexcept { return services_.ready(); }
private:
    LibnxHttpClient services_;
};
#endif

}  // namespace ttnx::switch_app
