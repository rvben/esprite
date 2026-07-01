#pragma once
#include <cstdint>

// Minimal esp_wifi shim: the Wi-Fi limits firmware round-trips the STA config
// (to preserve it across a WiFiManager reconfigure) and calls esp_wifi_connect.
// The sim has no radio, so config round-trips through an opaque blob and connect
// is a no-op. Return values are ignored by the firmware.

typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;

typedef struct { uint8_t opaque[132]; } wifi_config_t;

// Shared saved config (inline-function static: one instance across all TUs), so
// set/get actually round-trips the way the firmware's save-and-rollback path
// expects.
inline wifi_config_t& esp_wifi_saved_config() { static wifi_config_t c = {}; return c; }

inline int esp_wifi_get_config(wifi_interface_t, wifi_config_t* c) {
    if (c) *c = esp_wifi_saved_config();
    return 0;
}
inline int esp_wifi_set_config(wifi_interface_t, wifi_config_t* c) {
    if (c) esp_wifi_saved_config() = *c;
    return 0;
}
inline int esp_wifi_connect(void) { return 0; }
