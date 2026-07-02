#include "sim_ble.h"
#include <deque>

namespace {
struct BleBus {
    bool attached = false;
    SimBleLinkState state = SIM_BLE_IDLE;
    bool secure = false;
    uint32_t passkey = 0;
    std::deque<std::string> to_device;
    std::deque<std::string> to_host;
    std::vector<SimBleHidEvent> hid;
    std::string current_line;   // storage for the device's line-at-a-time reads
};
BleBus& bus() {
    static BleBus b;
    return b;
}
}  // namespace

void sim_ble_attach() {
    bus().attached = true;
    if (bus().state == SIM_BLE_IDLE) bus().state = SIM_BLE_ADVERTISING;
}

std::string sim_ble_device_next_line() {
    if (bus().to_device.empty()) return "";
    bus().current_line = bus().to_device.front();
    bus().to_device.pop_front();
    return bus().current_line;
}

void sim_ble_device_send(const std::string& line) {
    if (bus().state == SIM_BLE_CONNECTED) bus().to_host.push_back(line);
}

void sim_ble_device_drop_link() {
    if (!bus().attached) return;
    bus().state = SIM_BLE_ADVERTISING;
    bus().secure = false;
    bus().passkey = 0;
}

void sim_ble_device_hid_press(uint8_t key, uint8_t modifier) {
    if (bus().state == SIM_BLE_CONNECTED) bus().hid.push_back({key, modifier, true});
}
void sim_ble_device_hid_release() {
    if (bus().state == SIM_BLE_CONNECTED) bus().hid.push_back({0, 0, false});
}

bool sim_ble_available() { return bus().attached; }

void sim_ble_host_connect(uint32_t passkey) {
    if (!bus().attached) return;
    bus().state = SIM_BLE_CONNECTED;
    if (passkey == 0) {
        // Bonded fast path: encryption comes up without a passkey display.
        bus().secure = true;
        bus().passkey = 0;
    } else {
        // Passkey-entry pairing: the device displays the key until the host
        // confirms it (sim_ble_host_confirm_pairing).
        bus().secure = false;
        bus().passkey = passkey;
    }
}

void sim_ble_host_confirm_pairing() {
    if (bus().state != SIM_BLE_CONNECTED || bus().passkey == 0) return;
    bus().secure = true;
    bus().passkey = 0;
}

void sim_ble_host_disconnect() {
    sim_ble_device_drop_link();
}

void sim_ble_host_send(const std::string& line) {
    if (bus().state == SIM_BLE_CONNECTED) bus().to_device.push_back(line);
}

std::vector<std::string> sim_ble_host_drain() {
    std::vector<std::string> out(bus().to_host.begin(), bus().to_host.end());
    bus().to_host.clear();
    return out;
}

std::vector<SimBleHidEvent> sim_ble_hid_drain() {
    std::vector<SimBleHidEvent> out = bus().hid;
    bus().hid.clear();
    return out;
}

SimBleLinkState sim_ble_link_state() { return bus().state; }
bool            sim_ble_secure() { return bus().secure; }
uint32_t        sim_ble_passkey() { return bus().passkey; }

void sim_ble_reset() { bus() = BleBus{}; }

// Reset on every boot via the common runtime hook: a device power cycle drops
// the link and any queued data.
extern void sim_on_boot(void (*)());   // core/runtime
namespace { struct BootReg { BootReg() { sim_on_boot(sim_ble_reset); } } g_boot_reg; }
