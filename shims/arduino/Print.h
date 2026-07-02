#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <string>

// Serial/Print shim. Captures device output into a ring the CLI can search
// (serial expect) and drains host-injected input (serial send).
class HardwareSerial {
public:
    void   begin(long) {}
    void   flush() {}
    size_t write(uint8_t c);
    size_t write(const uint8_t* buf, size_t n);
    size_t print(const char* s);
    size_t print(int v);
    size_t print(unsigned v);
    size_t println(const char* s = "");
    size_t println(int v);
    int    printf(const char* fmt, ...);
    int    available();
    int    read();
};

extern HardwareSerial Serial;

// Sim-side observability and injection.
std::string sim_serial_output();
bool        sim_serial_contains(const std::string& regex);
bool        sim_serial_regex_valid(const std::string& regex);
void        sim_serial_inject(const std::string& data);
void        sim_serial_clear();

// Regex match over an arbitrary string, using the same mechanics as
// sim_serial_contains. Lets callers that hold serial output from elsewhere
// (e.g. a non-native SimBackend) match it the same way without duplicating
// the regex code sim_serial_contains uses on the native capture.
bool sim_serial_regex_search(const std::string& text, const std::string& regex);
