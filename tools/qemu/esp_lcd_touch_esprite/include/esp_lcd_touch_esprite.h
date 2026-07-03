// esp_lcd_touch driver over esprite's QEMU input agent: firmware that
// consumes the standard esp_lcd_touch interface (directly or through
// esp_lvgl_port) picks up host-injected touch with no esprite-specific
// application code. The QEMU fork models no touch controller, so this is
// the emulated stand-in for a CST816/GT911-style driver.
#pragma once
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates the driver. Call esprite_agent_start() once before the first
// read_data (the agent task owns the injected state this driver reports).
// config: only x_max/y_max are meaningful (bounds clamping); there is no
// interrupt or reset line.
esp_err_t esp_lcd_touch_new_esprite(const esp_lcd_touch_config_t* config,
                                    esp_lcd_touch_handle_t* out_touch);

#ifdef __cplusplus
}
#endif
