#pragma once
// Host stub for the ESP-IDF OTA partition API. The sim runs a single image that
// is already validated, so the running partition reports VALID (never
// PENDING_VERIFY): the firmware's trial/rollback gate stays inert and a normal
// boot proceeds. Enough surface for firmware that compiles the OTA path to link
// and run its non-flashing logic.
#include <cstdint>

typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK   0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif

typedef enum {
    ESP_OTA_IMG_NEW            = 0x0,
    ESP_OTA_IMG_PENDING_VERIFY = 0x1,
    ESP_OTA_IMG_VALID          = 0x2,
    ESP_OTA_IMG_INVALID        = 0x3,
    ESP_OTA_IMG_ABORTED        = 0x4,
    ESP_OTA_IMG_UNDEFINED      = 0xFFFFFFFF,
} esp_ota_img_states_t;

// Minimal partition handle: the firmware only checks it for non-null and passes
// it back to esp_ota_get_state_partition.
typedef struct { int _sim; } esp_partition_t;

inline const esp_partition_t* esp_ota_get_running_partition(void) {
    static esp_partition_t p{};
    return &p;
}
inline esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* state) {
    if (state) *state = ESP_OTA_IMG_VALID;   // the sim image is already validated
    return ESP_OK;
}
inline esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { return ESP_OK; }
// The sim cannot reboot into another slot; report failure so the firmware's
// fallback keeps the current image instead of expecting a reboot that won't come.
inline esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { return ESP_FAIL; }
