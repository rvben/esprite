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

TEST_CASE("webserver delivers a real POST body to the registered handler") {
    setenv("CLAWDSIM_HTTP_PORT", "18080", 1);
    WebServer server(80);
    std::string captured;
    server.on("/snapshot", HTTP_POST, [&]() {
        if (server.hasArg("plain")) captured = server.arg("plain").c_str();
        server.send(200, "text/plain", "ok\n");
    });
    server.begin();
    http_post(18080, "/snapshot", "{\"lim\":1,\"s5\":42}");
    server.handleClient();
    CHECK(captured == "{\"lim\":1,\"s5\":42}");
}
