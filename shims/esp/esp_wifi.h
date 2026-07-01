#pragma once
#include <cstdint>

// Minimal esp_wifi shim: the Wi-Fi limits firmware round-trips the STA config
// (to preserve it across a WiFiManager reconfigure) and calls esp_wifi_connect.
// The sim has no radio, so config round-trips through an opaque blob and connect
// is a no-op. Return values are ignored by the firmware.

typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;

typedef struct { uint8_t opaque[132]; } wifi_config_t;

inline int esp_wifi_get_config(wifi_interface_t, wifi_config_t* c) {
    if (c) *c = wifi_config_t{};
    return 0;
}
inline int esp_wifi_set_config(wifi_interface_t, wifi_config_t*) { return 0; }
inline int esp_wifi_connect(void) { return 0; }
