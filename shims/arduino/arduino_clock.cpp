#include "Arduino.h"
#include "Wire.h"

static uint32_t g_now_ms = 0;

uint32_t millis() { return g_now_ms; }
void delay(uint32_t ms) { g_now_ms += ms; }
void delayMicroseconds(uint32_t us) { g_now_ms += (us + 999) / 1000; }
void sim_clock_reset() { g_now_ms = 0; }
void sim_clock_advance(uint32_t ms) { g_now_ms += ms; }

// ---- Injected GPIO levels ----
static int g_gpio[64] = {0};
int  sim_gpio_get(int pin) { return (pin >= 0 && pin < 64) ? g_gpio[pin] : 0; }
void sim_gpio_set(int pin, int level) { if (pin >= 0 && pin < 64) g_gpio[pin] = level ? 1 : 0; }

void pinMode(int, int) {}
void digitalWrite(int pin, int level) { sim_gpio_set(pin, level); }
int  digitalRead(int pin) { return sim_gpio_get(pin); }

TwoWire  Wire;
EspClass ESP;
