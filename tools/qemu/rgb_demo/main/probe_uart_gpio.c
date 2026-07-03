// TEMPORARY M2 probe (replaced by esprite_qemu_agent): proves (a) UART1
// reaches the host's second -serial chardev in both directions, and (b) a
// pin reconfigured INPUT_OUTPUT reads back the level set by software, so a
// guest-side agent can inject GPIO the firmware's own gpio_get_level sees.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define PROBE_PIN 9

void probe_uart_gpio_start(void) {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &cfg));

    // GPIO probe: input+pullup like real button firmware, then drive it low
    // from software via INPUT_OUTPUT and read it back.
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PROBE_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    int before = gpio_get_level(PROBE_PIN);
    gpio_set_direction(PROBE_PIN, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(PROBE_PIN, 0);
    int low = gpio_get_level(PROBE_PIN);
    gpio_set_level(PROBE_PIN, 1);
    int high = gpio_get_level(PROBE_PIN);
    printf("gpio_probe before=%d low=%d high=%d verdict=%s\n", before, low, high,
           (low == 0 && high == 1) ? "LOOPBACK-OK" : "LOOPBACK-FAIL");

    uart_write_bytes(UART_NUM_1, "agent-probe hello\n", 18);
    uint8_t buf[128];
    for (;;) {
        int n = uart_read_bytes(UART_NUM_1, buf, sizeof(buf) - 1, pdMS_TO_TICKS(250));
        if (n > 0) {
            buf[n] = 0;
            printf("uart1 rx: %s\n", (char*)buf);            // proves host->guest, on the console
            uart_write_bytes(UART_NUM_1, "echo: ", 6);       // proves guest->host on UART1
            uart_write_bytes(UART_NUM_1, (const char*)buf, n);
        }
    }
}
