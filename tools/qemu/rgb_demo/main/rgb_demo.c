// QEMU display+input fixture: draws a fixed quadrant pattern on the fork's
// virtual RGB panel and reacts to esprite's input agent. esprite's gated
// tests assert it:
//   - quadrants TL red, TR green, BL blue, BR white (RGB565);
//   - a GPIO 9 button event (agent `pulse`) toggles color inversion
//     (red<->green, blue<->white swap);
//   - the LAST touch (held or released) leaves a latched 20x20 black square
//     centered on it - black appears in no quadrant, so a pixel probe is
//     unambiguous, and latching survives tap's press-then-release.
//
// Full-frame draws only, and state is polled via the agent's counters, not
// level edges: a draw blocks until a host capture consumes it (see the M1
// fixture notes), and a short pulse completing during that block would be
// missed by level polling while the event counter never is.
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "esprite_qemu_agent.h"

#define W 320
#define H 240
#define BTN_PIN 9
#define SQ 20

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

    esprite_agent_start();

    uint16_t *fb = malloc(W * H * sizeof(uint16_t));
    if (!fb) {
        printf("rgb_demo alloc failed\n");
        return;
    }

    int inv = 0, events_seen = 0;
    int have_sq = 0, sq_x = 0, sq_y = 0;
    printf("rgb_demo drawing\n");
    for (;;) {
        int dirty = 0;
        int events = esprite_agent_gpio_events(BTN_PIN);
        if (events != events_seen) {
            inv ^= (events - events_seen) & 1;
            events_seen = events;
            dirty = 1;
        }
        // Event counter, not the live held state: a host tap releases within
        // milliseconds and a 50 ms poll would miss it, while the counter
        // catches every press with its latest coordinates.
        static int touch_seen = 0;
        int tx, ty;
        int touches = esprite_agent_touch_events(&tx, &ty);
        if (touches != touch_seen) {
            touch_seen = touches;
            have_sq = 1;
            sq_x = tx;
            sq_y = ty;
            dirty = 1;
        }
        static int drawn_once = 0;
        if (dirty || !drawn_once) {
            drawn_once = 1;
            uint16_t tl = inv ? 0x07E0 : 0xF800, tr = inv ? 0xF800 : 0x07E0;
            uint16_t bl = inv ? 0xFFFF : 0x001F, br = inv ? 0x001F : 0xFFFF;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    fb[y * W + x] = (y < H / 2) ? (x < W / 2 ? tl : tr)
                                                : (x < W / 2 ? bl : br);
            if (have_sq)
                for (int y = sq_y - SQ / 2; y < sq_y + SQ / 2; y++)
                    for (int x = sq_x - SQ / 2; x < sq_x + SQ / 2; x++)
                        if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = 0x0000;
            // Blocks until a host-side capture consumes the frame.
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, fb));
            printf("rgb_demo state touch=%d inv=%d\n", have_sq, inv);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
