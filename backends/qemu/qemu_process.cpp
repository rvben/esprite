#include "qemu_process.h"
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
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
        "-drive", "file=" + spec.flash_image + ",if=mtd,format=raw",
        "-qmp", "unix:" + spec.qmp_socket + ",server=on,wait=off",
    };
    // Deliberately no "-monitor none": Task 2's spike found it intermittently
    // hangs this QEMU build, and Espressif's own pytest-embedded-qemu never
    // passes it either.
    if (spec.icount) {
        argv.push_back("-icount");
        argv.push_back("shift=3,align=off,sleep=off");
    }
    return argv;
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

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDERR_FILENO);
    // adddup2 leaves the original fd numbers open alongside the 0/1/2 dups;
    // close them explicitly so the child doesn't inherit spare pipe ends
    // (an extra open write end on out_pipe would keep the parent's read end
    // from ever seeing EOF, since the kernel only signals EOF once every
    // writer has closed).
    posix_spawn_file_actions_addclose(&actions, in_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t child = -1;
    int rc = posix_spawn(&child, argv[0].c_str(), &actions, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (rc != 0) {
        set_err(err, std::string("posix_spawn: ") + strerror(rc));
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

    in_fd = in_pipe[1];
    out_fd = out_pipe[0];
    pid = child;
    captured.clear();

    int flags = fcntl(out_fd, F_GETFL, 0);
    fcntl(out_fd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

bool QemuProcess::start(const QemuSpec& spec, std::string* err) {
    auto argv = qemu_build_argv(spec);
    if (!spawn_only(argv, err)) return false;

    auto deadline = Clock::now() + std::chrono::milliseconds(kQmpConnectWindowMs);
    std::string connect_err;
    for (;;) {
        if (qmp.connect_unix(spec.qmp_socket, kQmpConnectAttemptMs, &connect_err)) return true;
        if (!running()) {
            set_err(err, "qemu exited before the QMP socket became ready: " + connect_err);
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
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(in_fd, data.data() + off, data.size() - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;
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
        // debugger) must not be left a zombie.
        if (pid >= 0) {
            int status;
            waitpid(pid, &status, 0);
            pid = -1;
        }
    } else {
        qmp.close();
    }
    close_if_open(out_fd);
    close_if_open(in_fd);
}
