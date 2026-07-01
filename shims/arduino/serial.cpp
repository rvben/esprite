#include "Print.h"
#include <cstdio>
#include <deque>
#include <regex>

HardwareSerial Serial;

static std::string      g_out;   // captured device output
static std::deque<char> g_in;    // injected host-to-device input

size_t HardwareSerial::write(uint8_t c) { g_out.push_back((char)c); return 1; }
size_t HardwareSerial::write(const uint8_t* buf, size_t n) {
    g_out.append((const char*)buf, n);
    return n;
}
size_t HardwareSerial::print(const char* s) { g_out += s; return std::string(s).size(); }
size_t HardwareSerial::print(int v) { char b[16]; int n = snprintf(b, sizeof(b), "%d", v); g_out += b; return n; }
size_t HardwareSerial::print(unsigned v) { char b[16]; int n = snprintf(b, sizeof(b), "%u", v); g_out += b; return n; }
size_t HardwareSerial::println(const char* s) { g_out += s; g_out += '\n'; return std::string(s).size() + 1; }
size_t HardwareSerial::println(int v) { print(v); g_out += '\n'; return 0; }

int HardwareSerial::printf(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int copied = (n < 0) ? 0 : (n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
    g_out.append(buf, copied);
    return n;
}

int HardwareSerial::available() { return (int)g_in.size(); }
int HardwareSerial::read() {
    if (g_in.empty()) return -1;
    char c = g_in.front();
    g_in.pop_front();
    return (unsigned char)c;
}

std::string sim_serial_output() { return g_out; }
bool sim_serial_regex_valid(const std::string& rx) {
    try { std::regex probe(rx); return true; } catch (const std::regex_error&) { return false; }
}
bool sim_serial_contains(const std::string& rx) { return std::regex_search(g_out, std::regex(rx)); }
void sim_serial_inject(const std::string& d) { for (char c : d) g_in.push_back(c); }
void sim_serial_clear() { g_out.clear(); g_in.clear(); }
