#include "tizentube_nx/net/http1.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

}  // namespace

int main() {
    using namespace ttnx::net;

    const auto parsed_url = parse_https_url(
        "https://www.youtube.com/youtubei/v1/search?prettyPrint=false&alt=json#ignored");
    expect(parsed_url.has_value(), "HTTPS URL parses");
    if (parsed_url) {
        expect(parsed_url->host == "www.youtube.com", "host normalized");
        expect(parsed_url->port == 443, "default HTTPS port selected");
        expect(parsed_url->target == "/youtubei/v1/search?prettyPrint=false&alt=json",
               "fragment omitted from request target");
    }
    expect(!parse_https_url("http://www.youtube.com/"), "plain HTTP URL rejected");
    expect(!parse_https_url("https://user@www.youtube.com/"), "userinfo rejected");
    expect(!parse_https_url("https://[::1]/"), "IPv6 literal rejected");
    expect(!parse_https_url("https://www.youtube.com:444/"), "non-443 URL still parses only as explicit port");
    expect(!parse_https_url("https://-bad.youtube.com/"), "leading-hyphen DNS label rejected");
    expect(!parse_https_url("https://bad-.youtube.com/"), "trailing-hyphen DNS label rejected");
    expect(!parse_https_url("https://www.youtube.com/a path"), "space in request target rejected");

    HttpRequest post;
    post.method = HttpMethod::Post;
    post.url = "https://www.youtube.com/youtubei/v1/search";
    post.headers = {
        {"Content-Type", "application/json"},
        {"Host", "evil.example"},
        {"Connection", "keep-alive"},
        {"Accept-Encoding", "gzip"},
    };
    post.body = "{\"query\":\"switch\"}";

    const auto post_url = parse_https_url(post.url);
    const auto wire = post_url ? build_http1_request(post, *post_url) : std::nullopt;
    expect(wire.has_value(), "POST request serializes");
    if (wire) {
        expect(wire->starts_with("POST /youtubei/v1/search HTTP/1.1\r\n"),
               "POST request line correct");
        expect(wire->find("Host: www.youtube.com\r\n") != std::string::npos,
               "serializer owns Host header");
        expect(wire->find("evil.example") == std::string::npos,
               "caller cannot override Host");
        expect(wire->find("Connection: close\r\n") != std::string::npos,
               "serializer forces connection close");
        expect(wire->find("Accept-Encoding: identity\r\n") != std::string::npos,
               "serializer requests identity response encoding");
        expect(wire->find("Content-Length: 18\r\n") != std::string::npos,
               "POST Content-Length generated");
        expect(wire->ends_with(post.body), "POST body appended exactly");
    }

    HttpRequest injected = post;
    injected.headers.push_back({"X-Test", "safe\r\nHost: nintendo.com"});
    expect(post_url && !build_http1_request(injected, *post_url),
           "header CRLF injection rejected");

    HttpRequest control_header = post;
    control_header.headers.push_back({"X-Test", std::string("safe\x01bad", 8)});
    expect(post_url && !build_http1_request(control_header, *post_url),
           "header control characters rejected");

    const std::string fixed_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"ok\":true}";
    const auto fixed = parse_http1_response(fixed_response);
    expect(fixed.response.has_value(), "Content-Length response parses");
    if (fixed.response) {
        expect(fixed.response->status_code == 200, "status code parsed");
        expect(fixed.response->body == "{\"ok\":true}", "fixed body parsed");
        expect(find_header_value(*fixed.response, "content-type") == "application/json",
               "header lookup is case-insensitive");
    }

    const std::string chunked_response =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6;ext=yes\r\n world\r\n"
        "0\r\n"
        "X-Trailer: ignored\r\n"
        "\r\n";
    const auto chunked = parse_http1_response(chunked_response);
    expect(chunked.response.has_value(), "chunked response parses");
    if (chunked.response) {
        expect(chunked.response->body == "hello world", "chunked body decoded");
    }

    const std::string close_delimited =
        "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nbody-to-eof";
    const auto eof_body = parse_http1_response(close_delimited);
    expect(eof_body.response && eof_body.response->body == "body-to-eof",
           "connection-close body supported");

    const std::string redirect =
        "HTTP/1.1 302 Found\r\n"
        "Location: https://accounts.nintendo.com/\r\n"
        "Content-Length: 0\r\n\r\n";
    const auto parsed_redirect = parse_http1_response(redirect);
    expect(parsed_redirect.response.has_value(), "redirect response parses without following it");
    if (parsed_redirect.response) {
        expect(find_header_value(*parsed_redirect.response, "Location") ==
                   "https://accounts.nintendo.com/",
               "redirect Location is surfaced for policy validation");
    }

    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nabc").response,
           "unsupported transfer encoding rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n0\r\n\r\n").response,
           "ambiguous chunked plus Content-Length rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello").response,
           "duplicate Content-Length rejected even when values match");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").response,
           "duplicate Transfer-Encoding rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort").response,
           "truncated fixed body rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhelloEXTRA").response,
           "extra bytes after Content-Length rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\nabc\r\n0\r\n\r\n").response,
           "invalid chunk size rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n0\r\n\r\nEXTRA").response,
           "extra bytes after chunked terminator rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 2000 Nope\r\nContent-Length: 0\r\n\r\n").response,
           "malformed status code token rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 204 No Content\r\nContent-Length: 1\r\n\r\nx").response,
           "body on no-content status rejected");
    expect(!parse_http1_response(
                "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n123456", 5).response,
           "body limit enforced");

    if (failures == 0) {
        std::cout << "All HTTP/1.1 codec tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
