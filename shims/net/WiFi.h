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

class WiFiClass {
public:
    void mode(int) {}
    int  status() { return WL_CONNECTED; }
    IPAddress localIP() { return IPAddress(); }
    void reconnect() {}
    void disconnect(bool = false) {}
    const char* macAddress() { return "AA:BB:CC:DD:EE:FF"; }
    int    RSSI() { return -55; }
    String SSID() { return String("sim-wifi"); }
};

extern WiFiClass WiFi;
