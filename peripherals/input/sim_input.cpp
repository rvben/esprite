#include "sim_input.h"

static SimInput g_input;

SimInput& sim_input() { return g_input; }
void sim_input_reset() { g_input = SimInput{}; }

// Reset the injected input bus on every boot (via the common runtime hook) so a
// fresh boot starts with clean state and nothing leaks across reset-isolated
// runs. Injection happens after boot, so this never discards intended input.
extern void sim_on_boot(void (*)());   // core/runtime
namespace { struct Reg { Reg() { sim_on_boot(sim_input_reset); } } g_reg; }
