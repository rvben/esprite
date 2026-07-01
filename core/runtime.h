#pragma once
#include <string>

struct SimTarget;

// Boot the named target: select it, reset the virtual clock, and call its
// setup() once. Returns false if the target key is unknown.
bool sim_boot(const std::string& target_key);

// Advance the simulation by one loop() iteration.
void sim_step();
void sim_run_steps(int n);

// Run loop() until the virtual clock has advanced by at least `ms` (bounded by
// max_steps). Step counts alone cannot guarantee a display refresh: the clock
// only moves when the firmware delays, and LVGL repaints on a timer (33 ms
// default period). The 50 ms default covers one full refresh, so a frame
// captured after settling reflects every prior state change.
void sim_settle_ms(unsigned ms = 50, int max_steps = 400);

const SimTarget* sim_active_target();

// Register a callback invoked at the start of every sim_boot(), so subsystems
// (e.g. the LVGL snapshot ref map) can reset their per-session state on the
// common boot path regardless of how boot was reached.
void sim_on_boot(void (*cb)());
