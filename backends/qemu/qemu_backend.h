#pragma once

// Installs the qemu SimBackend implementation into core's backend registry
// (core/backend.h's sim_backend_register) so BACKEND_QEMU targets route
// through a real QEMU child process (backends/qemu/qemu_process.h) instead
// of falling back to native. Call once, before any boot path runs - cli.cpp
// does this at the top of esprite_main. Core never links against qemu code
// directly; the registration hook keeps that layering one-directional, and
// this is the one place that wires the two together.
void qemu_backend_install();
