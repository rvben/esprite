#include "agent_link.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>

namespace {

using Clock = std::chrono::steady_clock;

int ms_left(Clock::time_point deadline) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? (int)left : 0;
}

void set_err(std::string* err, const std::string& msg) { if (err) *err = msg; }

}  // namespace

AgentLink::~AgentLink() { close(); }

void AgentLink::close() {
    if (fd >= 0) { ::close(fd); fd = -1; }
    rxbuf.clear();
}

bool AgentLink::connect_unix(const std::string& socket_path, int timeout_ms, std::string* err) {
    (void)timeout_ms;   // unix-socket connect completes or fails immediately
    close();
    fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { set_err(err, std::string("socket: ") + strerror(errno)); return false; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        set_err(err, "agent socket path too long: " + socket_path);
        close();
        return false;
    }
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        set_err(err, "agent connect " + socket_path + ": " + strerror(errno));
        close();
        return false;
    }
    return true;
}

// Reads one newline-terminated line into *line (newline stripped), honoring
// the deadline via poll(). Returns false on timeout or disconnect.
static bool read_line(int fd, std::string* rxbuf, std::string* line,
                      Clock::time_point deadline, std::string* err) {
    for (;;) {
        size_t nl = rxbuf->find('\n');
        if (nl != std::string::npos) {
            *line = rxbuf->substr(0, nl);
            rxbuf->erase(0, nl + 1);
            if (!line->empty() && line->back() == '\r') line->pop_back();
            return true;
        }
        int left = ms_left(deadline);
        if (left == 0) { set_err(err, "agent reply timeout"); return false; }
        pollfd p{fd, POLLIN, 0};
        int pr = ::poll(&p, 1, left);
        if (pr <= 0) { set_err(err, pr == 0 ? "agent reply timeout" : std::string("poll: ") + strerror(errno)); return false; }
        char buf[512];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) { set_err(err, "agent disconnected"); return false; }
        rxbuf->append(buf, (size_t)n);
    }
}

bool AgentLink::request(const std::string& line, std::string* reply, std::string* err,
                        int timeout_ms) {
    if (fd < 0) { set_err(err, "agent not connected"); return false; }
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string out = line + "\n";
    size_t off = 0;
    while (off < out.size()) {
        ssize_t n = ::send(fd, out.data() + off, out.size() - off, 0);
        if (n <= 0) { set_err(err, std::string("agent send: ") + strerror(errno)); return false; }
        off += (size_t)n;
    }
    std::string got;
    if (!read_line(fd, &rxbuf, &got, deadline, err)) return false;
    // The guest may have written its startup banner after we connected but
    // before our first request; it is the only line that can precede a reply
    // in the strict request/reply protocol. Skip exactly one.
    if (got.rfind("esprite-agent", 0) == 0 && got.rfind("ok", 0) != 0) {
        if (!read_line(fd, &rxbuf, &got, deadline, err)) return false;
    }
    if (reply) *reply = got;
    if (got.rfind("ok", 0) == 0) return true;
    if (got.rfind("err ", 0) == 0) { set_err(err, got.substr(4)); return false; }
    set_err(err, "agent replied with an unrecognized line: " + got);
    return false;
}
