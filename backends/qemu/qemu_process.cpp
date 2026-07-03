#include "qemu_process.h"
#include <spawn.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>

// posix_spawn needs the process environment; declaring it extern is the
// portable way to reach it (both glibc and Apple's libSystem export the
// symbol, even though macOS does not expose it via <unistd.h>).
extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;

void set_err(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

void set_errno_err(std::string* err, const char* what) {
    set_err(err, std::string(what) + ": " + strerror(errno));
}

void close_if_open(int& fd) {
    if (fd >= 0) { ::close(fd); fd = -1; }
}

// Moves fd to a number above STDERR_FILENO via F_DUPFD, closing the original;
// a no-op that returns fd unchanged when it's already safe. See
// qemu_needs_fd_normalize's comment in qemu_process.h for why this has to
// run before spawn_only() builds its file actions.
int normalize_fd(int fd, std::string* err) {
    if (!qemu_needs_fd_normalize(fd)) return fd;
    int moved = fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
    if (moved < 0) { set_errno_err(err, "fcntl(F_DUPFD)"); return -1; }
    ::close(fd);
    return moved;
}

bool set_nonblocking(int fd, std::string* err) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { set_errno_err(err, "fcntl(F_GETFL)"); return false; }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        set_errno_err(err, "fcntl(F_SETFL, O_NONBLOCK)");
        return false;
    }
    return true;
}

// Bound on how long serial_write() will block waiting for the child to drain
// its stdin. A guest that stops reading (hung firmware, crashed before UART
// init) must not wedge the CLI's `serial send` forever; 2s is generous for a
// live QEMU child under normal load while still being a bounded contract a
// caller can rely on.
constexpr int kSerialWriteDeadlineMs = 2000;

// Retry window for the QMP connect inside start(): QEMU only creates the QMP
// unix socket partway through boot, and boot itself can take seconds under
// host load (Task 2, empirical), so this must be generous rather than tight.
constexpr int kQmpConnectWindowMs = 5000;
// Per-attempt connect_unix timeout: short, so a dead/slow attempt doesn't eat
// a big chunk of the overall retry window before the next attempt starts.
constexpr int kQmpConnectAttemptMs = 250;
// Gap between attempts while the socket file doesn't exist yet.
constexpr int kQmpConnectRetryDelayUs = 50000;

constexpr int kStopTermWaitMs = 2000;
constexpr int kStopKillWaitMs = 1000;

// Polls running() (which reaps via non-blocking waitpid) until the child
// exits or timeout_ms elapses. Returns whether the child had exited by then.
bool wait_for_exit(QemuProcess& p, int timeout_ms) {
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (!p.running()) return true;
        if (Clock::now() >= deadline) return false;
        usleep(10000);
    }
}

}  // namespace

std::vector<std::string> qemu_build_argv(const QemuSpec& spec) {
    std::vector<std::string> argv = {
        spec.qemu_bin,
        "-machine", spec.machine,
        "-nographic",
    };
    if (!spec.agent_socket.empty()) {
        // Explicit -serial flags replace -nographic's implicit slot-0
        // assignment in order: keep the console on the stdio mux, then hand
        // UART1 to the input-agent chardev.
        argv.insert(argv.end(), {"-serial", "mon:stdio",
                                 "-serial", "unix:" + spec.agent_socket + ",server=on,wait=off"});
    }
    argv.insert(argv.end(), {
        // snapshot=on: guest flash writes land in a throwaway overlay, so a
        // boot never mutates the user's image file and a read-only image
        // (e.g. a root-owned CI cache artifact) still boots - QEMU otherwise
        // opens mtd drives for writing.
        "-drive", "file=" + spec.flash_image + ",if=mtd,format=raw,snapshot=on",
        "-qmp", "unix:" + spec.qmp_socket + ",server=on,wait=off",
    });
    if (spec.http_host_port > 0 && spec.http_guest_port > 0) {
        // User-mode networking on the machine's OpenCores ethernet; the
        // forward is how `snapshot` reaches the guest's HTTP server.
        argv.insert(argv.end(), {"-nic",
            "user,model=open_eth,hostfwd=tcp:127.0.0.1:" +
            std::to_string(spec.http_host_port) + "-:" +
            std::to_string(spec.http_guest_port)});
    }
    // Deliberately no "-monitor none": Task 2's spike found it intermittently
    // hangs this QEMU build, and Espressif's own pytest-embedded-qemu never
    // passes it either.
    if (spec.icount) {
        argv.push_back("-icount");
        argv.push_back("shift=3,align=off,sleep=off");
    }
    return argv;
}

bool qemu_needs_fd_normalize(int fd) {
    return fd >= 0 && fd <= STDERR_FILENO;
}

int allocate_ephemeral_port(std::string* err) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (err) *err = std::string("socket: ") + strerror(errno);
        return 0;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t len = sizeof(a);
    int port = 0;
    if (::bind(fd, (sockaddr*)&a, sizeof(a)) == 0 &&
        ::getsockname(fd, (sockaddr*)&a, &len) == 0) {
        port = ntohs(a.sin_port);
    } else if (err) {
        *err = std::string("bind/getsockname: ") + strerror(errno);
    }
    ::close(fd);
    return port;
}

QemuProcess::~QemuProcess() { stop(); }

bool QemuProcess::spawn_only(const std::vector<std::string>& argv, std::string* err) {
    if (argv.empty()) { set_err(err, "empty argv"); return false; }

    // A serial_write() after the child has exited (or closed its stdin) hits
    // a broken pipe; default SIGPIPE disposition would kill the whole
    // simulator instead of letting write() fail with EPIPE (same fix as
    // WebServer::begin() applies to its client sockets).
    signal(SIGPIPE, SIG_IGN);

    int in_pipe[2];   // parent writes in_pipe[1] -> child reads in_pipe[0] as stdin
    int out_pipe[2];  // child writes out_pipe[1] (stdout+stderr) -> parent reads out_pipe[0]
    if (pipe(in_pipe) != 0) { set_errno_err(err, "pipe(stdin)"); return false; }
    if (pipe(out_pipe) != 0) {
        set_errno_err(err, "pipe(stdout)");
        close_if_open(in_pipe[0]);
        close_if_open(in_pipe[1]);
        return false;
    }

    // Move any pipe fd that landed on 0/1/2 out of the way before the file
    // actions below reference them by number (qemu_needs_fd_normalize's
    // header comment has the failure mode this avoids).
    int* pipe_fds[] = {&in_pipe[0], &in_pipe[1], &out_pipe[0], &out_pipe[1]};
    for (int* fd : pipe_fds) {
        int moved = normalize_fd(*fd, err);
        if (moved < 0) {
            close_if_open(in_pipe[0]);
            close_if_open(in_pipe[1]);
            close_if_open(out_pipe[0]);
            close_if_open(out_pipe[1]);
            return false;
        }
        *fd = moved;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // Each posix_spawn_file_actions_* call below can itself fail (e.g. ENOMEM
    // recording the action); rc/which track the first failure so it is
    // reported precisely instead of silently building a partial action list
    // that posix_spawn would then apply incompletely.
    int rc = 0;
    const char* which = nullptr;
    auto add = [&](int r, const char* what) { if (rc == 0 && r != 0) { rc = r; which = what; } };
    add(posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO), "adddup2(stdin)");
    add(posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO), "adddup2(stdout)");
    add(posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDERR_FILENO), "adddup2(stderr)");
    // adddup2 leaves the original fd numbers open alongside the 0/1/2 dups;
    // close them explicitly so the child doesn't inherit spare pipe ends
    // (an extra open write end on out_pipe would keep the parent's read end
    // from ever seeing EOF, since the kernel only signals EOF once every
    // writer has closed).
    add(posix_spawn_file_actions_addclose(&actions, in_pipe[0]), "addclose(in_pipe[0])");
    add(posix_spawn_file_actions_addclose(&actions, in_pipe[1]), "addclose(in_pipe[1])");
    add(posix_spawn_file_actions_addclose(&actions, out_pipe[0]), "addclose(out_pipe[0])");
    add(posix_spawn_file_actions_addclose(&actions, out_pipe[1]), "addclose(out_pipe[1])");
    if (rc != 0) {
        set_err(err, std::string("posix_spawn_file_actions_") + which + ": " + strerror(rc));
        posix_spawn_file_actions_destroy(&actions);
        close_if_open(in_pipe[0]);
        close_if_open(in_pipe[1]);
        close_if_open(out_pipe[0]);
        close_if_open(out_pipe[1]);
        return false;
    }

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t child = -1;
    int spawn_rc = posix_spawn(&child, argv[0].c_str(), &actions, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_rc != 0) {
        set_err(err, std::string("posix_spawn: ") + strerror(spawn_rc));
        close_if_open(in_pipe[0]);
        close_if_open(in_pipe[1]);
        close_if_open(out_pipe[0]);
        close_if_open(out_pipe[1]);
        return false;
    }

    // The child now owns its dup'd 0/1/2; the parent's ends of those same
    // pipe pairs (the halves it never wanted to give the child) are closed.
    close_if_open(in_pipe[0]);
    close_if_open(out_pipe[1]);

    // Both ends are non-blocking: out_fd for pump()'s non-blocking drain,
    // in_fd so serial_write() can poll(POLLOUT)-bound its writes instead of
    // wedging forever against a guest that stops reading its stdin.
    in_fd = in_pipe[1];
    out_fd = out_pipe[0];
    pid = child;
    captured.clear();

    if (!set_nonblocking(out_fd, err) || !set_nonblocking(in_fd, err)) { stop(); return false; }
    return true;
}

bool QemuProcess::start(const QemuSpec& spec, std::string* err) {
    auto argv = qemu_build_argv(spec);
    if (!spawn_only(argv, err)) return false;
    interrupted = spec.interrupted;   // serial_write's deadline loop bails early on this too

    auto deadline = Clock::now() + std::chrono::milliseconds(kQmpConnectWindowMs);
    std::string connect_err;
    for (;;) {
        if (qmp.connect_unix(spec.qmp_socket, kQmpConnectAttemptMs, &connect_err)) return true;
        if (!running()) {
            set_err(err, "qemu exited before the QMP socket became ready: " + connect_err);
            stop();
            return false;
        }
        if (spec.interrupted && spec.interrupted()) {
            set_err(err, "interrupted");
            stop();
            return false;
        }
        if (Clock::now() >= deadline) {
            set_err(err, "timed out waiting for QMP to become ready: " + connect_err);
            stop();
            return false;
        }
        usleep(kQmpConnectRetryDelayUs);
    }
}

bool QemuProcess::running() {
    if (pid < 0) return false;
    for (;;) {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == 0) return true;                      // still running
        if (r < 0 && errno == EINTR) continue;         // interrupted by a handled signal; retry
        // r == pid: reaped, exited or signaled. r < 0 otherwise (e.g.
        // ECHILD): already reaped elsewhere. Either way nothing left to
        // wait for.
        pid = -1;
        return false;
    }
}

void QemuProcess::pump() {
    if (out_fd < 0) return;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(out_fd, buf, sizeof(buf));
        if (n > 0) { captured.append(buf, (size_t)n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;   // nothing pending right now
        if (n < 0 && errno == EINTR) continue;
        // n == 0 (EOF) or a genuine read error: the child closed its end (or
        // something went wrong) and nothing more will ever arrive here.
        close_if_open(out_fd);
        return;
    }
}

bool QemuProcess::serial_write(const std::string& data) {
    if (in_fd < 0) return false;
    auto deadline = Clock::now() + std::chrono::milliseconds(kSerialWriteDeadlineMs);
    size_t off = 0;
    while (off < data.size()) {
        if (interrupted && interrupted()) return false;
        auto remaining = deadline - Clock::now();
        if (remaining <= std::chrono::milliseconds(0)) return false;   // deadline exceeded

        struct pollfd pfd { in_fd, POLLOUT, 0 };
        int pr = ::poll(&pfd, 1, (int)std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        if (pr < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (pr == 0) return false;   // timed out waiting for the pipe to become writable
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;   // child closed/gone

        ssize_t n = ::write(in_fd, data.data() + off, data.size() - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;   // poll's readiness was stale; retry
        if (n < 0 && errno == EINTR) continue;
        return false;   // genuine write error, e.g. EPIPE from a dead child
    }
    return true;
}

void QemuProcess::stop() {
    if (pid >= 0) {
        if (qmp.connected()) {
            std::string result, quit_err;
            qmp.execute("quit", "", &result, &quit_err, 2000);
        }
        qmp.close();

        if (running()) {
            kill(pid, SIGTERM);
            if (!wait_for_exit(*this, kStopTermWaitMs)) {
                kill(pid, SIGKILL);
                wait_for_exit(*this, kStopKillWaitMs);
            }
        }
        // Defensive final reap: running()'s non-blocking waitpid should
        // already have reaped the child above, but a child that somehow
        // survives SIGKILL's wait window (e.g. stuck reaping under a
        // debugger) must not be left a zombie. Loop on EINTR exactly like
        // running() does; a signal interrupting this blocking wait must not
        // be mistaken for a completed reap.
        if (pid >= 0) {
            int status;
            pid_t r;
            do { r = waitpid(pid, &status, 0); } while (r < 0 && errno == EINTR);
            pid = -1;
        }
    } else {
        qmp.close();
    }
    close_if_open(out_fd);
    close_if_open(in_fd);
}
