#include "runtime.h"
#include "target.h"
#include "Arduino.h"
#include <cstdlib>
#include <vector>

static const SimTarget* g_active = nullptr;

static std::vector<void (*)()>& boot_hooks() {
    static std::vector<void (*)()> v;
    return v;
}
void sim_on_boot(void (*cb)()) { if (cb) boot_hooks().push_back(cb); }

bool sim_boot(const std::string& key) {
    const SimTarget* t = sim_target(key);
    if (!t) return false;
    g_active = t;
    for (auto cb : boot_hooks()) cb();   // per-subsystem boot reset (e.g. UI refs)
    sim_clock_reset();
    sim_esp_restart_reset();             // clear sticky restart flag for a clean boot
    sim_gpio_reset();                    // clear injected GPIO levels for a clean boot
    // Default HTTP port for socket-backed webserver shims (targets can override
    // via the same env before boot).
    if (!getenv("ESPRITE_HTTP_PORT")) setenv("ESPRITE_HTTP_PORT", "8080", 1);
    if (t->setup) t->setup();
    return true;
}

void sim_step() { if (g_active && g_active->loop) g_active->loop(); }
void sim_run_steps(int n) {
    for (int i = 0; i < n && g_active && g_active->loop; ++i) g_active->loop();
}
void sim_settle_ms(unsigned ms, int max_steps) {
    unsigned start = millis();
    for (int i = 0; i < max_steps && millis() - start < ms; ++i) sim_step();
}

const SimTarget* sim_active_target() { return g_active; }
void sim_set_active_target(const SimTarget* t) { g_active = t; }
