#include "Print.h"
#include <cstdio>
#include <deque>
#include <regex>
#include <vector>

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
size_t HardwareSerial::println(int v) { size_t n = print(v); g_out += '\n'; return n + 1; }

int HardwareSerial::printf(const char* fmt, ...) {
    // Size the buffer to the formatted length, like Print::printf on the
    // device; a fixed buffer would silently truncate long debug output.
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) { va_end(ap2); return n; }
    std::vector<char> buf((size_t)n + 1);
    vsnprintf(buf.data(), buf.size(), fmt, ap2);
    va_end(ap2);
    g_out.append(buf.data(), (size_t)n);
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
