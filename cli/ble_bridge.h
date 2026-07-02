#pragma once

// Live BLE bridge: exposes the virtual BLE link as newline-delimited JSON over
// a localhost TCP socket, so a real host process (the desktop companion, a
// script, even nc) can drive a BLE firmware in the sim. One client at a time,
// mirroring a single BLE central: accept = connect (bonded), lines written =
// host->device, device->host lines stream back, close = disconnect.
//
// The bridge is a pumpable object (no thread, no loop of its own): serve's
// pump loop and tests call tick() alongside sim_run_steps.

struct BleBridge;

BleBridge* ble_bridge_open(int port);          // localhost; 0 = ephemeral
int        ble_bridge_port(const BleBridge*);  // the bound port
void       ble_bridge_tick(BleBridge*);        // accept + shuttle lines, non-blocking
void       ble_bridge_close(BleBridge*);
