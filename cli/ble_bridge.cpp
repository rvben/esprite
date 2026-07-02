#include "ble_bridge.h"
#include "sim_ble.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <string>

struct BleBridge {
    int listen_fd = -1;
    int client_fd = -1;
    int port = 0;
    std::string inbuf;   // bytes from the client, split on newlines
};

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

BleBridge* ble_bridge_open(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return nullptr;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        close(fd);
        return nullptr;
    }
    set_nonblocking(fd);
    if (port == 0) {
        sockaddr_in actual{};
        socklen_t len = sizeof(actual);
        if (getsockname(fd, (sockaddr*)&actual, &len) == 0)
            port = ntohs(actual.sin_port);
    }
    BleBridge* b = new BleBridge();
    b->listen_fd = fd;
    b->port = port;
    return b;
}

int ble_bridge_port(const BleBridge* b) { return b ? b->port : 0; }

static void drop_client(BleBridge* b) {
    if (b->client_fd >= 0) {
        close(b->client_fd);
        b->client_fd = -1;
        b->inbuf.clear();
        sim_ble_host_disconnect();
        fprintf(stderr, "ble-bridge: client disconnected\n");
    }
}

void ble_bridge_tick(BleBridge* b) {
    if (!b) return;

    // One central at a time: accept when free, refuse extras.
    int fd = accept(b->listen_fd, nullptr, nullptr);
    if (fd >= 0) {
        if (b->client_fd >= 0) {
            close(fd);
        } else {
            set_nonblocking(fd);
            b->client_fd = fd;
            sim_ble_host_connect(0);   // bonded fast path, like a paired desktop
            fprintf(stderr, "ble-bridge: client connected\n");
        }
    }
    if (b->client_fd < 0) return;

    // Client -> device: read whatever is pending, deliver complete lines.
    char buf[4096];
    for (;;) {
        ssize_t n = recv(b->client_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            b->inbuf.append(buf, (size_t)n);
            if (b->inbuf.size() > 65536) { drop_client(b); return; }   // runaway line
            continue;
        }
        if (n == 0) { drop_client(b); return; }                        // EOF
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        drop_client(b);                                                // error
        return;
    }
    size_t nl;
    while ((nl = b->inbuf.find('\n')) != std::string::npos) {
        std::string line = b->inbuf.substr(0, nl);
        b->inbuf.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) sim_ble_host_send(line);
    }

    // Device -> client: stream out anything the firmware sent.
    for (const std::string& l : sim_ble_host_drain()) {
        std::string out = l + "\n";
        size_t off = 0;
        while (off < out.size()) {
            ssize_t n = send(b->client_fd, out.data() + off, out.size() - off, 0);
            if (n > 0) { off += (size_t)n; continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            drop_client(b);
            return;
        }
    }
}

void ble_bridge_close(BleBridge* b) {
    if (!b) return;
    drop_client(b);
    if (b->listen_fd >= 0) close(b->listen_fd);
    delete b;
}
