#include "Arduino.h"
#include "Wire.h"

static uint32_t g_now_ms = 0;

uint32_t millis() { return g_now_ms; }
void delay(uint32_t ms) { g_now_ms += ms; }
void delayMicroseconds(uint32_t us) { g_now_ms += (us + 999) / 1000; }
void sim_clock_reset() { g_now_ms = 0; }
void sim_clock_advance(uint32_t ms) { g_now_ms += ms; }

TwoWire Wire;
