#include "esp_lcd_touch_esprite.h"
#include "esprite_qemu_agent.h"
#include <stdlib.h>
#include <string.h>

static esp_err_t read_data(esp_lcd_touch_handle_t tp) {
    int x = 0, y = 0;
    bool held = esprite_agent_touch(&x, &y);
    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = held ? 1 : 0;
    if (held) {
        if (x > tp->config.x_max) x = tp->config.x_max;
        if (y > tp->config.y_max) y = tp->config.y_max;
        tp->data.coords[0].x = (uint16_t)x;
        tp->data.coords[0].y = (uint16_t)y;
        tp->data.coords[0].strength = 1;
    }
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t* x, uint16_t* y,
                   uint16_t* strength, uint8_t* point_num, uint8_t max_point_num) {
    portENTER_CRITICAL(&tp->data.lock);
    *point_num = tp->data.points > max_point_num ? max_point_num : tp->data.points;
    for (uint8_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) strength[i] = tp->data.coords[i].strength;
    }
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return *point_num > 0;
}

static esp_err_t del(esp_lcd_touch_handle_t tp) {
    free(tp);
    return ESP_OK;
}

esp_err_t esp_lcd_touch_new_esprite(const esp_lcd_touch_config_t* config,
                                    esp_lcd_touch_handle_t* out_touch) {
    if (!config || !out_touch) return ESP_ERR_INVALID_ARG;
    esp_lcd_touch_handle_t tp = calloc(1, sizeof(esp_lcd_touch_t));
    if (!tp) return ESP_ERR_NO_MEM;
    tp->read_data = read_data;
    tp->get_xy = get_xy;
    tp->del = del;
    tp->config = *config;
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    tp->data.lock = lock;
    *out_touch = tp;
    return ESP_OK;
}
