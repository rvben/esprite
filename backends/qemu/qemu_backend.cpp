#include "qemu_backend.h"
#include "backend.h"
#include "target.h"
#include "qemu_process.h"
#include <ArduinoJson.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

// Wall-clock warmup after a successful spawn + QMP handshake: gives the
// guest a beat to print its early boot output (ROM bootloader, app startup)
// before the very first `logs`/`serial expect` call sees it. PROVISIONAL:
// unlike native's millis() shim, a qemu child runs in real time with no
// virtual-clock hook to drive deterministically, so this is a fixed
// wall-clock stand-in until a virtual-time mechanism lands. Never fails
// boot() itself - a firmware that prints nothing at all during this window
// is still a valid tier-1 boot (see the interface note on settle_ms: "do
// not wait for serial bytes").
constexpr unsigned kBootSettleMs = 500;

void set_err(std::string* err, const std::string& msg) { if (err) *err = msg; }

// Uppercases an ASCII arch string ("riscv32" -> "RISCV32") to build the
// ESPRITE_QEMU_<ARCH> env var name from a target's QemuMachineSpec.arch.
std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

std::string env_str(const char* name) {
    const char* v = getenv(name);
    return v ? v : "";
}

// Resolves the qemu-system-<arch> binary: ESPRITE_QEMU_BIN is an explicit
// override; otherwise ESPRITE_QEMU_<ARCH> (per the target's
// QemuMachineSpec.arch, e.g. ESPRITE_QEMU_RISCV32 / ESPRITE_QEMU_XTENSA -
// the same contract .qemu/env.sh exports). Verified executable here so a
// missing/typo'd path is reported with the exact piece named, rather than a
// bare posix_spawn errno surfacing later.
bool resolve_qemu_bin(const std::string& arch, std::string* bin, std::string* err) {
    std::string b = env_str("ESPRITE_QEMU_BIN");
    std::string arch_var = "ESPRITE_QEMU_" + upper(arch);
    if (b.empty()) b = env_str(arch_var.c_str());
    if (b.empty()) {
        set_err(err, "no qemu binary configured: set ESPRITE_QEMU_BIN or " + arch_var);
        return false;
    }
    if (access(b.c_str(), X_OK) != 0) {
        set_err(err, "qemu binary not found or not executable: " + b);
        return false;
    }
    *bin = b;
    return true;
}

bool resolve_image(std::string* image, std::string* err) {
    std::string p = env_str("ESPRITE_QEMU_IMAGE");
    if (p.empty()) {
        set_err(err, "ESPRITE_QEMU_IMAGE is not set (flash image path)");
        return false;
    }
    if (access(p.c_str(), R_OK) != 0) {
        set_err(err, "flash image not found: " + p);
        return false;
    }
    *image = p;
    return true;
}

}  // namespace

// Boots a BACKEND_QEMU target's firmware in a child QEMU process (QemuProcess,
// backends/qemu/qemu_process.h) and confirms over QMP that it is actually
// running, rather than executing Arduino code in this process like
// NativeBackend does. Singleton like the native backend: one qemu child per
// process.
struct QemuBackend : SimBackend {
    bool boot(const SimTarget* t, std::string* err) override {
        shutdown();   // idempotent; never let a previous boot's child outlive this one

        if (!t || !t->qemu) { set_err(err, "target has no qemu machine spec"); return false; }

        std::string bin, image;
        if (!resolve_qemu_bin(t->qemu->arch, &bin, err)) return false;
        if (!resolve_image(&image, err)) return false;

        // A fresh scratch dir per boot, NOT the state dir: sun_path is 104
        // bytes on macOS and a user's state-dir path can overflow it, while
        // /tmp/esprite-qemu.XXXXXX stays short and collision-free.
        char tmpl[] = "/tmp/esprite-qemu.XXXXXX";
        char* dir = mkdtemp(tmpl);
        if (!dir) { set_err(err, std::string("mkdtemp: ") + strerror(errno)); return false; }
        socket_dir_ = dir;

        QemuSpec spec;
        spec.qemu_bin = bin;
        spec.machine = t->qemu->machine;
        spec.flash_image = image;
        // icount trades wall-clock realism for deterministic instruction-paced
        // execution; only verified for riscv32 so far, xtensa stays wall-clock.
        spec.icount = std::string(t->qemu->arch) == "riscv32";
        spec.qmp_socket = socket_dir_ + "/qmp.sock";
        spec.interrupted = interrupted_;   // lets start()'s QMP retry loop bail early on SIGINT/SIGTERM

        if (!process_.start(spec, err)) { cleanup_dir(); return false; }

        if (interrupted_ && interrupted_()) {
            // Signaled between spawn and here (or start() itself already saw
            // it and unwound via spec.interrupted). Report it the same way,
            // stop the child, and let boot_or_die's backend_unavailable path
            // unwind normally so BackendShutdownGuard still runs.
            set_err(err, "interrupted");
            process_.stop();
            cleanup_dir();
            return false;
        }

        // Boot success = spawned + QMP negotiated + query-status confirms the
        // machine is actually running (not just that the socket answered).
        std::string result, qerr;
        if (!process_.qmp.execute("query-status", "", &result, &qerr)) {
            set_err(err, "query-status failed: " + qerr);
            process_.stop();
            cleanup_dir();
            return false;
        }
        JsonDocument doc;
        if (deserializeJson(doc, result)) {
            set_err(err, "query-status returned malformed JSON: " + result);
            process_.stop();
            cleanup_dir();
            return false;
        }
        std::string status = doc["status"] | "";
        if (status != "running") {
            set_err(err, "qemu is not running after boot (status='" + status + "')");
            process_.stop();
            cleanup_dir();
            return false;
        }

        if (!settle_ms(kBootSettleMs)) {   // best-effort warmup; see the comment above
            set_err(err, "interrupted");
            shutdown();
            return false;
        }
        return true;
    }

    void shutdown() override {
        process_.stop();
        cleanup_dir();
    }

    // Returns false if interrupted before the deadline (an early bail-out,
    // not a pump failure); boot() treats that as a failed boot. Callers that
    // only want the warmup pump (e.g. cli.cpp's serial settle) ignore the
    // return value, same as before this counted interruption.
    bool settle_ms(unsigned ms) override {
        auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        process_.pump();
        while (Clock::now() < deadline) {
            if (interrupted_ && interrupted_()) return false;
            usleep(10000);
            process_.pump();
        }
        return true;
    }

    void tick() override { process_.pump(); }

    std::string serial_output() override { process_.pump(); return process_.serial_output(); }

    bool serial_inject(const std::string& data) override { return process_.serial_write(data); }

    const char* name() const override { return "qemu"; }

    void set_interrupt_check(bool (*fn)()) { interrupted_ = fn; }

private:
    // Removes the mkdtemp scratch dir this boot created (its qmp socket, then
    // the directory itself). Idempotent: safe when nothing was ever created.
    void cleanup_dir() {
        if (socket_dir_.empty()) return;
        unlink((socket_dir_ + "/qmp.sock").c_str());
        rmdir(socket_dir_.c_str());
        socket_dir_.clear();
    }

    QemuProcess process_;
    std::string socket_dir_;
    // Optional CLI-supplied signal-flag accessor; null when nothing wired one
    // up (e.g. a unit test that never calls the (interrupted) overload of
    // qemu_backend_install), in which case boot's wait loops just never bail
    // early, matching the pre-signal-handling behavior.
    bool (*interrupted_)() = nullptr;
};

static QemuBackend g_qemu;

void qemu_backend_install(bool (*interrupted)()) {
    g_qemu.set_interrupt_check(interrupted);
    sim_backend_register(BACKEND_QEMU, &g_qemu);
}
