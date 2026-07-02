#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Virtual BLE link: a line-oriented bus between the host (the CLI, standing in
// for the Claude desktop app) and a device-side consumer (a firmware's BLE HAL
// adapter). Link state, pairing, and HID capture live here; protocol framing
// and JSON stay in the firmware, exactly as on hardware. Target-agnostic, like
// the input bus: injection commands work the same for any BLE firmware.

enum SimBleLinkState {
    SIM_BLE_IDLE = 0,      // no device stack attached
    SIM_BLE_ADVERTISING,   // device up, no host connected
    SIM_BLE_CONNECTED,     // host connected (secure once paired/bonded)
};

// ---- Device side (called by a firmware's BLE adapter) ----
void        sim_ble_attach();                    // the device stack came up
std::string sim_ble_device_next_line();          // next host->device line, "" when drained
void        sim_ble_device_send(const std::string& line);   // device->host line
void        sim_ble_device_drop_link();          // device-initiated drop (e.g. clear bonds)
void        sim_ble_device_hid_press(uint8_t key, uint8_t modifier);
void        sim_ble_device_hid_release();

// ---- Host side (called by the CLI) ----
bool sim_ble_available();                        // a device stack is attached
void sim_ble_host_connect(uint32_t passkey);     // passkey 0 = bonded fast path
void sim_ble_host_confirm_pairing();             // "the desktop entered the passkey"
void sim_ble_host_disconnect();
void sim_ble_host_send(const std::string& line);
std::vector<std::string> sim_ble_host_drain();   // device->host lines

struct SimBleHidEvent {
    uint8_t key;
    uint8_t modifier;
    bool    press;   // false = release
};
std::vector<SimBleHidEvent> sim_ble_hid_drain();

// ---- Shared state ----
SimBleLinkState sim_ble_link_state();
bool            sim_ble_secure();                // encrypted/bonded link
uint32_t        sim_ble_passkey();               // 6-digit key being displayed, 0 = none
void            sim_ble_reset();                 // full reset (also runs on every boot)
