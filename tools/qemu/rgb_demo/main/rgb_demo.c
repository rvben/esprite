// QEMU display fixture: draws a fixed quadrant pattern on the fork's virtual
// RGB panel (esp_rgb device, driven by the esp_lcd_qemu_rgb component), then
// prints a serial marker and redraws once per second. The pattern is asserted
// by esprite's gated display tests: TL red, TR green, BL blue, BR white.
//
// Full-frame draws only: esp_rgb consumes a draw (and clears the busy flag
// the driver spins on) in its console refresh callback, which headless QEMU
// runs only when something pumps the console - one QMP screendump = one
// consumed draw. Per-line drawing would need one screendump per line and
// wedges headless; one draw per frame matches how esprite's sync_framebuffer
// polls the panel. The first "rgb_demo ready" therefore appears right after
// the first screendump consumes the initial frame.
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"

#define W 320
#define H 240

void app_main(void) {
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_qemu_config_t cfg = {
        .width = W,
        .height = H,
        .bpp = RGB_QEMU_BPP_16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    uint16_t *fb = malloc(W * H * sizeof(uint16_t));
    if (!fb) {
        printf("rgb_demo alloc failed\n");
        return;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            fb[y * W + x] = (y < H / 2) ? (x < W / 2 ? 0xF800 : 0x07E0)    // red | green
                                        : (x < W / 2 ? 0x001F : 0xFFFF);   // blue | white
        }
    }
    printf("rgb_demo drawing\n");
    for (;;) {
        // Blocks until a host-side console refresh (e.g. a screendump)
        // consumes the frame.
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, fb));
        printf("rgb_demo ready\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
