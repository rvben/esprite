#pragma once
#include "Arduino.h"

// Provisioning shim. autoConnect returns connected instantly. The AP callback
// path is available so a target can screenshot its captive-portal hint by
// setting the env CLAWDSIM_WIFI_PORTAL=1 before boot.
class WiFiManager {
public:
    typedef void (*ApCb)(WiFiManager*);
    void setConfigPortalTimeout(int) {}
    void setAPCallback(ApCb cb) { ap_cb_ = cb; }
    bool autoConnect(const char* ap_ssid = nullptr) {
        const char* portal = getenv("CLAWDSIM_WIFI_PORTAL");
        if (portal && portal[0] == '1' && ap_cb_) { ap_cb_(this); return false; }
        (void)ap_ssid;
        return true;
    }
private:
    ApCb ap_cb_ = nullptr;
};
