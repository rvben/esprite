#pragma once
#include "qmp.h"
#include <string>
#include <vector>
#include <sys/types.h>

// QEMU boot spec: pins the child binary, target machine, flash image, and
// QMP control socket path (caller chooses the directory, typically a
// mkdtemp'd scratch dir alongside the flash image). icount pins deterministic
// virtual-time execution (used by later gated tests); off by default so the
// child just runs at wall-clock speed.
struct QemuSpec {
    std::string qemu_bin;      // qemu-system-riscv32 / -xtensa
    std::string machine;       // "esp32c3" | "esp32" | "esp32s3"
    std::string flash_image;   // merged flash image (raw, mtd)
    bool        icount = false;        // -icount shift=3,align=off,sleep=off
    std::string qmp_socket;            // unix socket path (caller-chosen dir)
    // Non-empty = wire UART1 to this unix-socket chardev for the guest input
    // agent (esprite_qemu_agent). UART0 stays on the -nographic stdio mux via
    // an explicit -serial mon:stdio (explicit -serial flags replace the
    // implicit assignment in order).
    std::string agent_socket;
    // Optional poll hook for start()'s bounded QMP connect retry: checked each
    // spin so a SIGINT/SIGTERM during boot bails out early instead of blocking
    // through the full window. Backends/qemu must not include cli headers, so
    // the caller (cli.cpp, via qemu_backend_install) hands its own signal-flag
    // accessor down through here rather than this layer reaching up for one.
    bool (*interrupted)() = nullptr;
};

// Pure and unit-testable: builds the exact child argv from spec. No side
// effects (no spawn, no filesystem access) - see the argv contract in
// tests/test_qemu_process.cpp for the flags this must always/never emit.
std::vector<std::string> qemu_build_argv(const QemuSpec& spec);

// True when fd lands on a standard stream (0/1/2) and must be moved before
// spawn_only() builds its posix_spawn_file_actions_t: a parent that starts
// with any of its own stdio fds already closed (a daemon/supervisor launcher
// habit) leaves pipe() free to hand back 0/1/2, and dup2(fd, fd) for a fd
// already at its target is a same-fd no-op that only clears CLOEXEC - the
// addclose(fd) queued right after it would then close the fd the child was
// supposed to inherit. Pure decision, no syscalls, so this fd-collision case
// is unit-testable without needing to fake actual fd exhaustion.
bool qemu_needs_fd_normalize(int fd);

// Owns one QEMU child process end to end: spawn, QMP handshake, non-blocking
// stdio serial capture, and an escalating stop(). Not copyable: pid, the two
// pipe fds, and the QmpClient member are all single-owner resources that
// stop() (called from the destructor) assumes exactly one QemuProcess
// instance is responsible for - same rationale as QmpClient's own deleted
// copy ops. Move is implicitly absent for the same reason (a user-declared
// destructor plus deleted copy ops suppresses the implicit move members).
struct QemuProcess {
    QemuProcess() = default;
    ~QemuProcess();
    QemuProcess(const QemuProcess&) = delete;
    QemuProcess& operator=(const QemuProcess&) = delete;

    // Builds argv from spec, spawns the child, then connects QMP with
    // retries spanning a generous window (the QMP unix socket is only
    // created partway through QEMU's boot, and boot itself can take seconds
    // under host load - see kQmpConnectWindowMs in qemu_process.cpp). On
    // failure the child is stopped before returning.
    bool start(const QemuSpec& spec, std::string* err);

    // Spawns argv directly with no QMP step: the process plumbing start()
    // composes on top of. Exposed (not just an implementation detail) so
    // lifecycle - spawn, capture, stop - is testable against a stand-in
    // child like /bin/cat instead of a real QEMU binary.
    bool spawn_only(const std::vector<std::string>& argv, std::string* err);

    // Escalating, idempotent shutdown: QMP "quit" if connected, then SIGTERM
    // (~2s wait), then SIGKILL (~1s wait), always finishing with a waitpid
    // reap and fd close. Safe to call when nothing was ever spawned, and
    // safe to call more than once.
    void stop();

    // True if the child is still alive. Reaps a just-exited child as a side
    // effect (non-blocking waitpid), so stop() can rely on it to tell
    // whether escalation is still needed.
    bool running();

    // Non-blocking: drains whatever the child has written to its merged
    // stdout+stderr pipe since the last pump() into captured. Never blocks,
    // even when the child has written nothing.
    void pump();

    std::string serial_output() const { return captured; }

    // Writes to the child's stdin (its UART0 under -nographic), bounded by
    // kSerialWriteDeadlineMs total (a guest that stops draining stdin must
    // not hang the caller forever). Polls POLLOUT rather than blocking in
    // write(), and bails early - returning false - the moment `interrupted`
    // (set from QemuSpec::interrupted by start(); left null when spawn_only()
    // is used directly, e.g. in tests) reports a pending signal. Returns
    // false on a write error too (e.g. the child has already exited) or on
    // hitting the deadline; a partial write is not undone; only bytes never
    // attempted are the reported failure.
    bool serial_write(const std::string& data);

    QmpClient qmp;
    pid_t pid = -1; int out_fd = -1; int in_fd = -1; std::string captured;
    // Same interrupt accessor as QemuSpec::interrupted (see its comment):
    // start() copies it here so serial_write(), called long after start()
    // returns, can still bail out on a pending signal without needing a
    // QemuSpec passed back in.
    bool (*interrupted)() = nullptr;
};
