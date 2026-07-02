#pragma once

// Installs the qemu SimBackend implementation into core's backend registry
// (core/backend.h's sim_backend_register) so BACKEND_QEMU targets route
// through a real QEMU child process (backends/qemu/qemu_process.h) instead
// of falling back to native. Call once, before any boot path runs - cli.cpp
// does this at the top of esprite_main. Core never links against qemu code
// directly; the registration hook keeps that layering one-directional, and
// this is the one place that wires the two together.
//
// interrupted: optional accessor for the CLI's process-wide SIGINT/SIGTERM
// flag, polled by the backend's own bounded wait loops (the QMP connect
// retry, boot settle) so a signal during a qemu boot bails out early instead
// of blocking to a timeout. Passed as a function pointer, not a header
// include, so backends/qemu stays cli-free. Idempotent and safe to call from
// more than one entry point (esprite_main and esprite_daemon both do).
void qemu_backend_install(bool (*interrupted)() = nullptr);
