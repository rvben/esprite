#include "WebServer.h"
#include "WiFi.h"
#include "ESPmDNS.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstdio>

WiFiClass     WiFi;
MDNSResponder MDNS;

WebServer::~WebServer() {
    if (listen_fd_ >= 0) close(listen_fd_);
}

void WebServer::on(const char* path, int method, Handler h) {
    if (method == HTTP_POST) post_[path] = h;
    else                     get_[path] = h;
}

void WebServer::begin() {
    signal(SIGPIPE, SIG_IGN);   // a client that closed before we reply must not kill us
    const char* env = getenv("CLAWDSIM_HTTP_PORT");
    bound_port_ = env ? atoi(env) : port_;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)bound_port_);
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(listen_fd_); listen_fd_ = -1; return;
    }
    listen(listen_fd_, 8);
    fcntl(listen_fd_, F_SETFL, O_NONBLOCK);
}

void WebServer::handleClient() {
    if (listen_fd_ < 0) return;
    int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) return;
    // The accepted socket may inherit O_NONBLOCK from the listener on macOS;
    // force it blocking with a short recv timeout so the request body is read
    // reliably without ever wedging the single-threaded pump.
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    timeval tv{0, 200000};   // 200 ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[16384];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = 0;
    std::string req(buf, (size_t)n);

    std::string method = req.substr(0, req.find(' '));
    size_t p1 = req.find(' ');
    std::string path;
    if (p1 != std::string::npos) {
        p1 += 1;
        size_t p2 = req.find(' ', p1);
        path = req.substr(p1, p2 - p1);
    }
    size_t bodyPos = req.find("\r\n\r\n");
    body_ = (bodyPos == std::string::npos) ? "" : req.substr(bodyPos + 4);

    client_fd_ = fd;
    auto& table = (method == "POST") ? post_ : get_;
    auto it = table.find(path);
    if (it != table.end()) it->second();
    else send(404, "text/plain", String("not found\n"));
    close(client_fd_);
    client_fd_ = -1;
}

void WebServer::send(int code, const char* type, const String& body) {
    if (client_fd_ < 0) return;
    std::string b = body.c_str();
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        code, type, b.size());
    ::send(client_fd_, hdr, (size_t)hn, 0);
    ::send(client_fd_, b.data(), b.size(), 0);
}

bool WebServer::hasArg(const char* name) {
    return std::string(name) == "plain" && !body_.empty();
}
String WebServer::arg(const char* name) {
    return (std::string(name) == "plain") ? String(body_.c_str()) : String("");
}
