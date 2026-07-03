// A real LVGL application running under esprite's QEMU backend. Display and
// input go through the standard component stack a product would use (lvgl +
// esp_lvgl_port + esp_lcd_qemu_rgb; touch arrives later via the standard
// esp_lcd_touch interface). The one hard constraint of the emulated panel:
// the esp_rgb device consumes ONE pending draw per host-side capture and the
// driver busy-waits per draw_bitmap, so LVGL runs in full_refresh mode (one
// full-frame flush per LVGL frame); partial dirty-region flushes would need
// several captures per frame and stall the UI to the capture rate.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "esp_lvgl_port.h"
#include "esprite_qemu_agent.h"

#define W 320
#define H 240

// Full frame in .bss (see the display-glue comment in app_main).
static uint16_t s_frame[W * H];

// One full-frame draw per LVGL frame; the driver's draw_bitmap busy-waits
// until the host consumes it, which paces the UI to the capture rate.
static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    esp_lcd_panel_handle_t p = lv_display_get_user_data(d);
    esp_lcd_panel_draw_bitmap(p, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
    lv_display_flush_ready(d);
}

void app_main(void) {
    esprite_agent_start();

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_qemu_config_t panel_cfg = {
        .width = W,
        .height = H,
        .bpp = RGB_QEMU_BPP_16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    // Hand-rolled display glue instead of lvgl_port_add_disp: the port only
    // heap-allocates its draw buffer, and a full 320x240 RGB565 frame
    // (required by the emulated panel's one-draw-per-capture model) does not
    // fit the C3's heap next to the port's defaults - a static buffer does.
    // Everything here is standard LVGL 9 API; the port still owns the task,
    // tick, and locking.
    lvgl_port_lock(0);
    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_frame, NULL, sizeof(s_frame),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, flush_cb);
    lvgl_port_unlock();

    // Probe UI: a solid indicator panel (asserted by pixel value from the
    // host) and a label, all standard widgets.
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x202020), 0);
    lv_obj_t* indicator = lv_obj_create(scr);
    lv_obj_set_size(indicator, 80, 80);
    lv_obj_align(indicator, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_radius(indicator, 0, 0);
    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, "lvgl demo");
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lvgl_port_unlock();

    printf("lvgl_demo ready\n");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
