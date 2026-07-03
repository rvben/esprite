#include "backend.h"
#include "runtime.h"
#include "Print.h"
#include "WebServer.h"

// Steps to run after boot so the firmware's first frame (and any UI refs it
// registers) settle before the CLI acts on it. Moved here from cli.cpp: it is
// now part of what "boot" means for a backend, not a CLI concern.
static const int WARMUP_STEPS = 60;

// The historical in-process behavior, now behind the SimBackend interface.
// File-static singleton: there is exactly one native backend per process.
struct NativeBackend : SimBackend {
    bool boot(const SimTarget* t, std::string* err) override {
        if (!t || !sim_boot(t->key)) {
            if (err) *err = "unknown target '" + std::string(t ? t->key : "") + "'";
            return false;
        }
        sim_run_steps(WARMUP_STEPS);
        return true;
    }
    void shutdown() override {}
    bool settle_ms(unsigned ms) override { sim_settle_ms(ms); return true; }
    void tick() override { sim_run_steps(4); }
    std::string serial_output() override { return sim_serial_output(); }
    bool serial_inject(const std::string& data) override { sim_serial_inject(data); return true; }
    int http_port() override {
        int p = sim_http_bind_status();
        return p > 0 ? p : 0;
    }
    const char* name() const override { return "native"; }
};

static NativeBackend g_native;
static SimBackend* g_active = &g_native;

// Backends other than native register themselves here (Task 6:
// qemu_backend_install() calls this once). Indexed by kind so
// sim_backend_select can look one up without core linking against qemu code.
static SimBackend*& registered(SimBackendKind kind) {
    static SimBackend* slots[2] = {&g_native, nullptr};   // [BACKEND_NATIVE], [BACKEND_QEMU]
    return slots[kind];
}

SimBackend& sim_backend() { return *g_active; }

void sim_backend_select(const SimTarget* t) {
    SimBackendKind kind = t ? t->backend : BACKEND_NATIVE;
    SimBackend* impl = registered(kind);
    g_active = impl ? impl : &g_native;   // unregistered kind falls back to native
}

void sim_backend_register(SimBackendKind kind, SimBackend* impl) {
    registered(kind) = impl;
}
