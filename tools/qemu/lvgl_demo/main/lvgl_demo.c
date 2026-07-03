// A real LVGL application running under esprite's QEMU backend: a two-screen
// device control panel. Display and touch go through the standard component
// stack a product would use (lvgl + esp_lvgl_port task/tick + the standard
// esp_lcd_touch interface); the only esprite-specific application calls are
// starting the agent and polling its gpio event counter for the BOOT button
// (the fork's gpio model reflects nothing into gpio_get_level, so a real
// button driver cannot work - see the esprite README's fidelity matrix).
//
// The emulated panel's hard constraint: the esp_rgb device consumes ONE
// pending draw per host-side capture and the driver busy-waits per
// draw_bitmap, so LVGL runs in full-refresh mode with a single static
// full-frame buffer (one flush per frame). lvgl_port_add_disp is NOT used
// for the display: it heap-allocates its buffer with no external-buffer
// option, and a 150 KB frame does not fit the C3's heap next to the port's
// defaults.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_esprite.h"
#include "esp_lvgl_port.h"
#include "esprite_qemu_agent.h"

#define W 320
#define H 240
#define BOOT_PIN 9

static uint16_t s_frame[W * H];

static lv_obj_t* s_scr_control;
static lv_obj_t* s_scr_about;
static lv_obj_t* s_indicator;   // 80x80 at (30,60): red off, green on
static lv_obj_t* s_level_box;   // 40x40 at (260,160): blue channel = slider

static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    esp_lcd_panel_handle_t p = lv_display_get_user_data(d);
    esp_lcd_panel_draw_bitmap(p, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
    lv_display_flush_ready(d);
}

// The standard esp_lcd_touch -> LVGL indev bridge: poll read_data, report
// the pressed point. Exactly what a CST816/GT911 integration looks like.
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(tp);
    uint16_t x, y;
    uint8_t n = 0;
    if (esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &n, 1) && n > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void power_switch_cb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_indicator,
                              on ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
}

static void level_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);   // 0..255
    lv_obj_set_style_bg_color(s_level_box, lv_color_make(0, 0, (uint8_t)v), 0);
}

// BOOT cycles screens. Event counter, not level polling: a pulse can
// complete while the LVGL task is blocked in a flush awaiting its consumer.
static void boot_button_timer_cb(lv_timer_t* t) {
    (void)t;
    static int seen = 0;
    int events = esprite_agent_gpio_events(BOOT_PIN);
    if (events != seen) {
        seen = events;
        lv_obj_t* next = lv_screen_active() == s_scr_control ? s_scr_about : s_scr_control;
        lv_screen_load(next);
    }
}

static void build_ui(void) {
    // Screen 1: Control. Fixed positions, scrolling off, so host-side tap
    // coordinates and pixel assertions stay exact.
    s_scr_control = lv_screen_active();
    lv_obj_clear_flag(s_scr_control, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr_control, lv_color_hex(0x202020), 0);

    lv_obj_t* title = lv_label_create(s_scr_control);
    lv_label_set_text(title, "Device Control");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(title, 20, 12);

    s_indicator = lv_obj_create(s_scr_control);
    lv_obj_set_size(s_indicator, 80, 80);
    lv_obj_set_pos(s_indicator, 30, 60);
    lv_obj_set_style_bg_color(s_indicator, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(s_indicator, 0, 0);
    lv_obj_set_style_radius(s_indicator, 0, 0);

    lv_obj_t* sw = lv_switch_create(s_scr_control);
    lv_obj_set_size(sw, 60, 32);
    lv_obj_set_pos(sw, 200, 84);   // center (230,100)
    lv_obj_add_event_cb(sw, power_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* slider = lv_slider_create(s_scr_control);
    lv_slider_set_range(slider, 0, 255);
    lv_obj_set_size(slider, 200, 16);
    lv_obj_set_pos(slider, 30, 180);   // track x 30..230, y center 188
    lv_obj_add_event_cb(slider, level_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_level_box = lv_obj_create(s_scr_control);
    lv_obj_set_size(s_level_box, 40, 40);
    lv_obj_set_pos(s_level_box, 260, 168);
    lv_obj_set_style_bg_color(s_level_box, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_border_width(s_level_box, 0, 0);
    lv_obj_set_style_radius(s_level_box, 0, 0);

    // Screen 2: About.
    s_scr_about = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr_about, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr_about, lv_color_hex(0x000040), 0);
    lv_obj_t* about = lv_label_create(s_scr_about);
    lv_label_set_text(about, "lvgl_demo\nreal firmware under esprite");
    lv_obj_set_style_text_color(about, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(about);

    lv_timer_create(boot_button_timer_cb, 50, NULL);
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

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = W - 1,
        .y_max = H - 1,
    };
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_esprite(&touch_cfg, &touch));

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_lock(0);
    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_frame, NULL, sizeof(s_frame),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev, touch);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    build_ui();
    lvgl_port_unlock();

    printf("lvgl_demo ready\n");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
