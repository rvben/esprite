#include "doctest.h"
#include "WebServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>

static void http_post(int port, const std::string& path, const std::string& body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    REQUIRE(connect(fd, (sockaddr*)&a, sizeof(a)) == 0);
    char req[1024];
    int n = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: x\r\nContent-Length: %zu\r\n\r\n%s",
        path.c_str(), body.size(), body.c_str());
    ::send(fd, req, (size_t)n, 0);
    // Fire-and-forget: the single-threaded server processes this in a later
    // handleClient() call, so we must not block waiting for the response here.
    shutdown(fd, SHUT_WR);
    close(fd);
}

// Send a raw request and return the full response. The server is single-
// threaded: send everything, half-close, then alternate handleClient() with a
// bounded recv until the response arrives. A single handleClient() call can
// race the kernel's accept queue and process nothing, so poll.
static std::string http_roundtrip(WebServer& server, int port, const std::string& req) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    REQUIRE(connect(fd, (sockaddr*)&a, sizeof(a)) == 0);
    size_t off = 0;
    while (off < req.size()) {
        ssize_t n = ::send(fd, req.data() + off, req.size() - off, 0);
        if (n <= 0) break;
        off += (size_t)n;
    }
    shutdown(fd, SHUT_WR);
    timeval tv{0, 100000};   // 100 ms per recv attempt
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string resp;
    char buf[65536];
    ssize_t n = -1;
    for (int attempt = 0; attempt < 20 && n <= 0; ++attempt) {
        server.handleClient();
        n = recv(fd, buf, sizeof(buf), 0);
    }
    while (n > 0) {
        resp.append(buf, (size_t)n);
        n = recv(fd, buf, sizeof(buf), 0);
    }
    close(fd);
    return resp;
}

TEST_CASE("only headers declared via collectHeaders are retained (hardware semantics)") {
    // Real ESP32 WebServer keeps just the headers named in collectHeaders();
    // capturing everything would let firmware that forgot to declare one work
    // in the sim and silently fail on the device.
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    WebServer server(80);
    bool handled = false, has_known = false, has_secret = true;
    server.on("/h", HTTP_POST, [&]() {
        handled    = true;
        has_known  = server.hasHeader("X-Known");
        has_secret = server.hasHeader("X-Secret");
        server.send(200, "text/plain", "ok");
    });
    const char* keep[] = {"X-Known"};
    server.collectHeaders(keep, 1);
    server.begin();
    int port = sim_http_bind_status();
    REQUIRE(port > 0);
    http_roundtrip(server, port,
        "POST /h HTTP/1.1\r\nHost: x\r\nX-Known: 1\r\nX-Secret: 1\r\nContent-Length: 0\r\n\r\n");
    REQUIRE(handled);
    CHECK(has_known);
    CHECK_FALSE(has_secret);
    unsetenv("ESPRITE_HTTP_PORT");
}

TEST_CASE("the response status line carries the real reason phrase") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    WebServer server(80);
    server.on("/forbidden", HTTP_POST, [&]() { server.send(403, "text/plain", "no"); });
    server.begin();
    int port = sim_http_bind_status();
    REQUIRE(port > 0);
    std::string resp = http_roundtrip(server, port,
        "POST /forbidden HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    CHECK(resp.rfind("HTTP/1.1 403 Forbidden", 0) == 0);
    unsetenv("ESPRITE_HTTP_PORT");
}

TEST_CASE("webserver delivers a real POST body to the registered handler") {
    // Bind an ephemeral port so a leftover listener from a prior run can never
    // collide with a fixed port and make this test spuriously fail.
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    WebServer server(80);
    std::string captured;
    server.on("/snapshot", HTTP_POST, [&]() {
        if (server.hasArg("plain")) captured = server.arg("plain").c_str();
        server.send(200, "text/plain", "ok\n");
    });
    server.begin();
    int port = sim_http_bind_status();   // OS-assigned ephemeral port
    REQUIRE(port > 0);                    // bind succeeded
    http_post(port, "/snapshot", "{\"lim\":1,\"s5\":42}");
    for (int i = 0; i < 20 && captured.empty(); ++i) server.handleClient();
    CHECK(captured == "{\"lim\":1,\"s5\":42}");
    unsetenv("ESPRITE_HTTP_PORT");       // don't leak the ephemeral setting to other tests
}
