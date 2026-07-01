#include "runtime.h"
#include "target.h"
#include "Arduino.h"
#include <cstdlib>

static const SimTarget* g_active = nullptr;

bool sim_boot(const std::string& key) {
    const SimTarget* t = sim_target(key);
    if (!t) return false;
    g_active = t;
    sim_clock_reset();
    // Default HTTP port for socket-backed webserver shims (targets can override
    // via the same env before boot).
    if (!getenv("CLAWDSIM_HTTP_PORT")) setenv("CLAWDSIM_HTTP_PORT", "8080", 1);
    if (t->setup) t->setup();
    return true;
}

void sim_step() { if (g_active && g_active->loop) g_active->loop(); }
void sim_run_steps(int n) {
    for (int i = 0; i < n && g_active && g_active->loop; ++i) g_active->loop();
}

const SimTarget* sim_active_target() { return g_active; }
