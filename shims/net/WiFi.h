#pragma once
#include "Arduino.h"

#define WIFI_STA 1
#define WIFI_AP  2
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

class IPAddress {
public:
    IPAddress() {}
    String toString() const { return String("127.0.0.1"); }
};

// Simulated link state, settable via `esprite wifi up|down` (see actions.cpp).
// Defaults to connected so existing scenarios that never touch Wi-Fi are
// unaffected, and resets to connected on every sim_boot.
bool sim_wifi_connected();
void sim_wifi_set_connected(bool connected);

class WiFiClass {
public:
    void mode(int) {}
    int  status() { return sim_wifi_connected() ? WL_CONNECTED : WL_DISCONNECTED; }
    IPAddress localIP() { return IPAddress(); }
    // Real hardware's reconnect() only requests an attempt (the link comes
    // back on its own time, if at all); leaving this a no-op means a
    // simulated `wifi down` sticks until `wifi up` restores it, instead of
    // the firmware's own retry silently undoing the injected state.
    void reconnect() {}
    void disconnect(bool = false) { sim_wifi_set_connected(false); }
    const char* macAddress() { return "AA:BB:CC:DD:EE:FF"; }
    int    RSSI() { return -55; }
    String SSID() { return String("sim-wifi"); }
};

extern WiFiClass WiFi;
