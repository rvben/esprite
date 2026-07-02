#include "qmp.h"
#include <ArduinoJson.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>

namespace {

using Clock = std::chrono::steady_clock;

// Milliseconds remaining until deadline, clamped to zero (never negative, so
// callers can pass this straight to poll()).
int remaining_ms(Clock::time_point deadline) {
    auto now = Clock::now();
    if (now >= deadline) return 0;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return ms > 0 ? (int)ms : 0;
}

void set_err(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

void set_errno_err(std::string* err, const char* what) {
    set_err(err, std::string(what) + ": " + strerror(errno));
}

// Reads one newline-delimited line from fd, buffering any partial line in
// rxbuf across calls (a reply can arrive split across several recv()s, and a
// single recv() can also contain more than one line). Every wait honors the
// remaining time until deadline, so a chain of reads shares one overall
// budget instead of resetting a fresh timeout per line.
bool read_line(int fd, std::string& rxbuf, Clock::time_point deadline,
                std::string* line, std::string* err) {
    for (;;) {
        size_t nl = rxbuf.find('\n');
        if (nl != std::string::npos) {
            *line = rxbuf.substr(0, nl);
            rxbuf.erase(0, nl + 1);
            return true;
        }
        int wait_ms = remaining_ms(deadline);
        if (wait_ms <= 0) { set_err(err, "timed out waiting for a QMP reply"); return false; }
        pollfd pfd{fd, POLLIN, 0};
        int rv = poll(&pfd, 1, wait_ms);
        if (rv < 0) {
            if (errno == EINTR) continue;
            set_errno_err(err, "poll");
            return false;
        }
        if (rv == 0) { set_err(err, "timed out waiting for a QMP reply"); return false; }
        char buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            set_errno_err(err, "recv");
            return false;
        }
        if (n == 0) { set_err(err, "QMP peer closed the connection"); return false; }
        rxbuf.append(buf, (size_t)n);
    }
}

// send() flags that stop a write to a peer that already closed its end from
// raising SIGPIPE (default disposition: terminate the process) instead of
// just failing the call with EPIPE. Linux supports MSG_NOSIGNAL directly;
// macOS/BSD have no such flag (SO_NOSIGPIPE is a socket option, set once at
// connect time instead - see connect_unix).
#ifdef MSG_NOSIGNAL
static const int kSendFlags = MSG_NOSIGNAL;
#else
static const int kSendFlags = 0;
#endif

// Writes line plus a trailing newline to fd. The fd is non-blocking (shared
// with the poll()-based reads), so even a small QMP command can need more
// than one send() to land fully.
bool write_line(int fd, const std::string& line, Clock::time_point deadline, std::string* err) {
    std::string out = line;
    out.push_back('\n');
    size_t off = 0;
    while (off < out.size()) {
        int wait_ms = remaining_ms(deadline);
        if (wait_ms <= 0) { set_err(err, "timed out writing a QMP command"); return false; }
        pollfd pfd{fd, POLLOUT, 0};
        int rv = poll(&pfd, 1, wait_ms);
        if (rv < 0) {
            if (errno == EINTR) continue;
            set_errno_err(err, "poll");
            return false;
        }
        if (rv == 0) { set_err(err, "timed out writing a QMP command"); return false; }
        ssize_t n = send(fd, out.data() + off, out.size() - off, kSendFlags);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            set_errno_err(err, "send");
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

// A {"error":...} reply is a normal, expected QMP outcome (e.g. an unknown
// command name): the session stays perfectly usable for the next execute().
// Anything else that stops read_reply short - a read timeout, a malformed
// line, an unrecognized message - means the stream itself may now be
// desynced (a late reply to a timed-out command could still be sitting in
// the kernel buffer, ready to be misread as the next command's reply), so
// the caller must close rather than keep going.
enum class ReplyStatus { OK, APP_ERROR, TRANSPORT_ERROR };

// Reads QMP reply lines, skipping interleaved {"event":...} notifications,
// until a {"return":...} or {"error":...} arrives. On "return", *result (if
// non-null) gets the raw return value re-serialized as JSON. On "error",
// *err gets the error's "desc".
ReplyStatus read_reply(int fd, std::string& rxbuf, Clock::time_point deadline,
                        std::string* result, std::string* err) {
    for (;;) {
        std::string line;
        if (!read_line(fd, rxbuf, deadline, &line, err)) return ReplyStatus::TRANSPORT_ERROR;
        if (line.empty()) continue;   // tolerate a stray blank line

        JsonDocument doc;
        if (deserializeJson(doc, line)) {
            set_err(err, "malformed QMP line: " + line);
            return ReplyStatus::TRANSPORT_ERROR;
        }
        if (!doc["event"].isNull()) continue;   // async notification, not a reply: skip
        if (!doc["error"].isNull()) {
            set_err(err, doc["error"]["desc"] | std::string("unknown QMP error"));
            return ReplyStatus::APP_ERROR;
        }
        if (!doc["return"].isNull()) {
            if (result) serializeJson(doc["return"], *result);
            return ReplyStatus::OK;
        }
        set_err(err, "unexpected QMP message: " + line);
        return ReplyStatus::TRANSPORT_ERROR;
    }
}

}  // namespace

QmpClient::~QmpClient() { close(); }

void QmpClient::close() {
    if (fd >= 0) { ::close(fd); fd = -1; }
    rxbuf.clear();
}

bool QmpClient::connect_unix(const std::string& socket_path, int timeout_ms, std::string* err) {
    close();   // idempotent; a retried connect never leaks a previous fd
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

    sockaddr_un addr{};
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        set_err(err, "socket path too long");
        return false;
    }

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) { set_errno_err(err, "socket"); return false; }
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    // macOS/BSD: no MSG_NOSIGNAL send() flag exists, so ask the socket itself
    // to turn a write to a peer that closed its end into EPIPE instead of
    // raising SIGPIPE (default disposition: terminate the process).
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        // EINPROGRESS is the POSIX code for "nonblocking connect still
        // running"; Linux's unix(7) documents AF_UNIX stream connects using
        // EAGAIN for the same condition instead, so both must be treated as
        // in-progress rather than a hard failure.
        if (errno == EINPROGRESS || errno == EAGAIN) {
            pollfd pfd{s, POLLOUT, 0};
            int rv = poll(&pfd, 1, remaining_ms(deadline));
            if (rv <= 0) {
                set_err(err, rv == 0 ? "connect timed out" : (std::string("poll: ") + strerror(errno)));
                ::close(s);
                return false;
            }
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error != 0) {
                set_err(err, std::string("connect: ") + strerror(so_error));
                ::close(s);
                return false;
            }
        } else {
            set_errno_err(err, "connect");
            ::close(s);
            return false;
        }
    }

    fd = s;

    std::string greeting;
    if (!read_line(fd, rxbuf, deadline, &greeting, err)) { close(); return false; }
    JsonDocument gdoc;
    if (deserializeJson(gdoc, greeting) || gdoc["QMP"].isNull()) {
        set_err(err, "malformed QMP greeting: " + greeting);
        close();
        return false;
    }

    if (!write_line(fd, R"({"execute":"qmp_capabilities"})", deadline, err)) { close(); return false; }
    // Any non-OK outcome here (even an application-level "error") means
    // negotiation itself failed, so the connection is unusable either way.
    if (read_reply(fd, rxbuf, deadline, nullptr, err) != ReplyStatus::OK) { close(); return false; }
    return true;
}

bool QmpClient::execute(const std::string& cmd, const std::string& args_json,
                         std::string* result, std::string* err, int timeout_ms) {
    if (fd < 0) { set_err(err, "not connected"); return false; }
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

    JsonDocument doc;
    doc["execute"] = cmd;
    if (!args_json.empty()) {
        JsonDocument args;
        if (deserializeJson(args, args_json)) {
            set_err(err, "args_json is not valid JSON: " + args_json);
            return false;
        }
        doc["arguments"] = args;
    }
    std::string line;
    serializeJson(doc, line);

    if (!write_line(fd, line, deadline, err)) { close(); return false; }
    ReplyStatus st = read_reply(fd, rxbuf, deadline, result, err);
    if (st == ReplyStatus::TRANSPORT_ERROR) close();   // stream may be desynced; do not reuse it
    return st == ReplyStatus::OK;
}
