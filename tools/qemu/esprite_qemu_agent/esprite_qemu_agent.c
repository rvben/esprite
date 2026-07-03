#include "esprite_qemu_agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#define AGENT_UART UART_NUM_1
#define MAX_PIN 63

// Injected state, written by the agent task and polled by firmware tasks.
// Single-word volatile reads/writes are atomic on this core; the protocol is
// low-rate control traffic, so no stronger synchronization is needed.
static volatile bool s_touch_held = false;
static volatile int s_touch_x = 0, s_touch_y = 0;
static volatile signed char s_gpio_level[MAX_PIN + 1];   // -1 = never injected
static volatile int s_gpio_events[MAX_PIN + 1];

bool esprite_agent_touch(int* x, int* y) {
    if (!s_touch_held) return false;
    if (x) *x = s_touch_x;
    if (y) *y = s_touch_y;
    return true;
}

int esprite_agent_gpio_level(int pin, int default_level) {
    if (pin < 0 || pin > MAX_PIN) return default_level;
    signed char v = s_gpio_level[pin];
    return v < 0 ? default_level : v;
}

int esprite_agent_gpio_events(int pin) {
    if (pin < 0 || pin > MAX_PIN) return 0;
    return s_gpio_events[pin];
}

static void reply(const char* line) {
    uart_write_bytes(AGENT_UART, line, strlen(line));
    uart_write_bytes(AGENT_UART, "\n", 1);
}

// Parses a decimal int strictly (whole token, bounded); returns false on
// garbage so a malformed command never half-applies.
static bool parse_int(const char* tok, long min, long max, long* out) {
    if (!tok || !*tok) return false;
    char* end = NULL;
    long v = strtol(tok, &end, 10);
    if (*end != '\0' || v < min || v > max) return false;
    *out = v;
    return true;
}

static void handle_line(char* line) {
    char* save = NULL;
    char* cmd = strtok_r(line, " ", &save);
    if (!cmd) { reply("err empty command"); return; }
    if (strcmp(cmd, "ping") == 0) {
        reply("ok esprite-agent v1");
    } else if (strcmp(cmd, "gpio") == 0) {
        long pin, level;
        if (!parse_int(strtok_r(NULL, " ", &save), 0, MAX_PIN, &pin) ||
            !parse_int(strtok_r(NULL, " ", &save), 0, 1, &level)) {
            reply("err gpio needs <pin 0-63> <0|1>");
            return;
        }
        if (level == 0 && s_gpio_level[pin] != 0) s_gpio_events[pin]++;
        s_gpio_level[pin] = (signed char)level;
        reply("ok");
    } else if (strcmp(cmd, "pulse") == 0) {
        long pin, level, ms;
        if (!parse_int(strtok_r(NULL, " ", &save), 0, MAX_PIN, &pin) ||
            !parse_int(strtok_r(NULL, " ", &save), 0, 1, &level) ||
            !parse_int(strtok_r(NULL, " ", &save), 1, 10000, &ms)) {
            reply("err pulse needs <pin 0-63> <0|1> <ms 1-10000>");
            return;
        }
        s_gpio_level[pin] = (signed char)level;
        vTaskDelay(pdMS_TO_TICKS(ms));
        s_gpio_level[pin] = (signed char)!level;
        s_gpio_events[pin]++;
        reply("ok");
    } else if (strcmp(cmd, "touch") == 0) {
        long x, y;
        if (!parse_int(strtok_r(NULL, " ", &save), 0, 4095, &x) ||
            !parse_int(strtok_r(NULL, " ", &save), 0, 4095, &y)) {
            reply("err touch needs <x> <y>");
            return;
        }
        s_touch_x = (int)x;
        s_touch_y = (int)y;
        s_touch_held = true;
        reply("ok");
    } else if (strcmp(cmd, "release") == 0) {
        s_touch_held = false;
        reply("ok");
    } else {
        reply("err unknown command");
    }
}

static void agent_task(void* arg) {
    (void)arg;
    static char line[128];
    size_t len = 0;
    reply("esprite-agent v1");   // banner; dropped if no host is connected yet
    for (;;) {
        uint8_t c;
        int n = uart_read_bytes(AGENT_UART, &c, 1, portMAX_DELAY);
        if (n != 1) continue;
        if (c == '\n' || c == '\r') {
            if (len > 0) {
                line[len] = '\0';
                handle_line(line);
                len = 0;
            }
        } else if (len < sizeof(line) - 1) {
            line[len++] = (char)c;
        } else {
            len = 0;   // oversized line: drop and resync at the next newline
            reply("err line too long");
        }
    }
}

void esprite_agent_start(void) {
    memset((void*)s_gpio_level, 0xFF, sizeof(s_gpio_level));   // -1 everywhere
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(AGENT_UART, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(AGENT_UART, &cfg));
    xTaskCreate(agent_task, "esprite_agent", 4096, NULL, 5, NULL);
}
