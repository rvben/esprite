#pragma once
// Host shim for the Arduino core API surface used by compiled ESP32 apps.
// Chip- and app-agnostic. Coverage grows as targets need more; keep additions
// minimal and unit-tested.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

typedef uint8_t byte;
typedef bool    boolean;

// ---- Virtual clock ----------------------------------------------------------
// millis() is a monotonically advancing counter driven by delay() and
// sim_clock_advance(); it never reads wall-clock time, so a fixed number of
// loop() iterations always yields the same virtual time.
uint32_t millis();
void     delay(uint32_t ms);
void     delayMicroseconds(uint32_t us);
void     sim_clock_reset();
void     sim_clock_advance(uint32_t ms);

// ---- Math helpers -----------------------------------------------------------
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
template <typename T>
inline T constrain(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- GPIO stubs -------------------------------------------------------------
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
void pinMode(int pin, int mode);
void digitalWrite(int pin, int level);
int  digitalRead(int pin);
inline int analogRead(int) { return 0; }

// Injected GPIO state: the CLI and live window set levels here (a physical
// button wired to a pin, or `esprite gpio PIN LEVEL`), and digitalRead reads
// them, so standard-library sketches that poll pins work in the sim.
int  sim_gpio_get(int pin);
void sim_gpio_set(int pin, int level);
void sim_gpio_reset();   // clear all injected pin levels (called on each sim_boot)

// ---- Flash-string / PROGMEM no-ops -----------------------------------------
#define F(x) (x)
#define PROGMEM
#define PSTR(x) (x)

// ---- Minimal String ---------------------------------------------------------
// The compiled apps so far use only c_str()/length(); keep it thin. Extend as
// targets need concatenation/parsing.
class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
    String(unsigned v) { char b[16]; snprintf(b, sizeof(b), "%u", v); s_ = b; }
    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    String operator+(const String& o) const { return String(s_ + o.s_); }
    // ArduinoJson's String writer appends via concat(); return truthy on success.
    unsigned char concat(const char* s) { s_ += (s ? s : ""); return 1; }
    unsigned char concat(char c) { s_ += c; return 1; }
    String& operator+=(const char* s) { s_ += (s ? s : ""); return *this; }
    // Lets ArduinoJson and other APIs consume a String as a C string.
    operator const char*() const { return s_.c_str(); }
private:
    std::string s_;
};

// Minimal ESP system object. restart() cannot faithfully reboot the process mid
// call-stack, so it records the request (observable via serial and the query
// below) instead of silently doing nothing; a driver can assert the firmware
// reached its restart/forget path.
class EspClass {
public:
    void     restart();
    uint32_t getFreeHeap() { return 200000; }
};
extern EspClass ESP;

bool sim_esp_restart_requested();   // true once ESP.restart() has been called
void sim_esp_restart_reset();       // clear the flag (called on each sim_boot)

// strlcpy is BSD libc (present on macOS). Provide a fallback elsewhere.
#if defined(__linux__)
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    size_t len = strlen(src);
    if (size) { size_t n = len < size - 1 ? len : size - 1; memcpy(dst, src, n); dst[n] = 0; }
    return len;
}
#endif

#include "Print.h"   // Serial / HardwareSerial
