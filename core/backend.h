#pragma once
#include <string>
#include "target.h"

// Seam between the CLI and whatever actually runs a target's firmware. Native
// runs it in-process (the historical behavior, now delegated through here);
// Task 6 adds a qemu backend that boots the firmware in a child QEMU process.
// Boot paths and the serve-loop pump go through sim_backend(); scenario and
// the screenshot/ui/snapshot/input paths stay native-only for now.
struct SimBackend {
    virtual ~SimBackend() = default;
    virtual bool boot(const SimTarget* t, std::string* err) = 0;  // includes warmup
    virtual void shutdown() = 0;
    virtual bool settle_ms(unsigned ms) = 0;
    virtual void tick() = 0;                       // serve-loop pump; native = run steps
    virtual std::string serial_output() = 0;
    virtual bool serial_inject(const std::string& data) = 0;
    // Refresh sim_framebuffer() with the display's current pixels. Native
    // firmware renders in-process so the framebuffer is always current: the
    // default is a successful no-op. The qemu backend implements it with a
    // QMP screendump decoded into the framebuffer, so callers can always
    // sync-then-capture without branching on the backend.
    virtual bool sync_framebuffer(std::string* err) { (void)err; return true; }

    // Tier-2 input injection via a cooperating guest agent (esprite_qemu_agent
    // on a second UART chardev). Native input never routes here (it uses the
    // in-process input bus in cli/actions.cpp), so the defaults report
    // unavailability; the qemu backend implements them over its AgentLink
    // when the target's machine spec declares the agent.
    virtual bool agent_available() { return false; }
    virtual bool agent_gpio(int pin, int level, std::string* err) {
        (void)pin; (void)level;
        if (err) *err = "no input agent on this backend";
        return false;
    }
    virtual bool agent_pulse(int pin, int level, int ms, std::string* err) {
        (void)pin; (void)level; (void)ms;
        if (err) *err = "no input agent on this backend";
        return false;
    }
    virtual bool agent_touch(bool down, int x, int y, std::string* err) {
        (void)down; (void)x; (void)y;
        if (err) *err = "no input agent on this backend";
        return false;
    }

    // The localhost TCP port where the booted firmware's HTTP server is
    // reachable, or 0 when it has none. Native: the webserver shim's bound
    // port. Qemu: the user-net hostfwd port when the board spec declares an
    // http capability. sim_wifi_post targets this - one code path for both.
    virtual int http_port() { return 0; }
    virtual const char* name() const = 0;          // "native" | "qemu"
};

// The active backend (native until a qemu boot selects otherwise).
SimBackend& sim_backend();

// Selects the backend for the given target's SimBackendKind. Called by the
// boot paths (one-shot boot_or_die, serve, the daemon's boot command) before
// boot() so the rest of the session runs against the right implementation.
void sim_backend_select(const SimTarget* t);

// Layering: core knows only the native impl. Other backends register here
// (Task 6: cli calls qemu_backend_install() once at esprite_main entry) so
// core never links against qemu code. sim_backend_select falls back to native
// for any kind with no registered implementation.
void sim_backend_register(SimBackendKind kind, SimBackend* impl);
