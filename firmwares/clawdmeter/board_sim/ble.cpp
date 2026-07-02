#include "ble.h"
#include "sim_ble.h"
#include <string>

// Clawdmeter's BLE interface bound to the sim's virtual BLE link. The firmware
// side (protocol.cpp, app_buddy.cpp) is compiled unchanged; only this radio
// layer is swapped, like the display and input HALs. Deliberate behaviors:
//  - The link is host-driven: `esprite ble connect` stands in for the desktop
//    initiating a connection, with an optional passkey-entry pairing flow.
//  - ble_has_bonds() mirrors link security: a secure link means a stored bond,
//    and ble_clear_bonds() drops the link back to advertising, so the firmware's
//    pairing gesture and unpair command behave observably.

static std::string g_line;   // backing storage for ble_next_line

void ble_init(void) { sim_ble_attach(); }
void ble_tick(void) {}   // link transitions are host-driven; nothing to pump

ble_state_t ble_get_state(void) {
    switch (sim_ble_link_state()) {
    case SIM_BLE_CONNECTED:   return BLE_STATE_CONNECTED;
    case SIM_BLE_ADVERTISING: return BLE_STATE_ADVERTISING;
    default:                  return BLE_STATE_INIT;
    }
}

const char* ble_get_device_name(void) { return "Clawdmeter-Sim"; }
// RFC 7042 documentation MAC (00-00-5E-00-53-xx): never a real device.
const char* ble_get_mac_address(void) { return "00:00:5E:00:53:01"; }

void ble_clear_bonds(void) { sim_ble_device_drop_link(); }
bool ble_has_bonds(void)   { return sim_ble_secure(); }

const char* ble_next_line(void) {
    g_line = sim_ble_device_next_line();
    return g_line.empty() ? nullptr : g_line.c_str();
}

void ble_send_line(const char* json) {
    if (json) sim_ble_device_send(json);
}

uint32_t ble_get_passkey(void) { return sim_ble_passkey(); }
bool     ble_is_secure(void)   { return sim_ble_secure(); }

void ble_keyboard_press(uint8_t key, uint8_t modifier) { sim_ble_device_hid_press(key, modifier); }
void ble_keyboard_release(void)                        { sim_ble_device_hid_release(); }
