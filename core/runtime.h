#pragma once
#include <string>

struct SimTarget;

// Boot the named target: select it, reset the virtual clock, and call its
// setup() once. Returns false if the target key is unknown.
bool sim_boot(const std::string& target_key);

// Advance the simulation by one loop() iteration.
void sim_step();
void sim_run_steps(int n);

const SimTarget* sim_active_target();
