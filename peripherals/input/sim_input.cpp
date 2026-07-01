#include "sim_input.h"

static SimInput g_input;

SimInput& sim_input() { return g_input; }
void sim_input_reset() { g_input = SimInput{}; }
