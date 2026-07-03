#pragma once
#include <string>

// Line-protocol client for the guest input agent (esprite_qemu_agent over a
// second UART chardev on a unix socket). Strictly request/reply after
// connect: one line out (newline appended), one line back. A reply starting
// with "ok" resolves true (full reply text in *reply); "err <msg>" resolves
// false with <msg> in *err; timeouts and disconnects resolve false with a
// transport error. Same bounded-blocking poll() discipline as QmpClient:
// input injection is low-rate control traffic, never a hot path.
struct AgentLink {
    AgentLink() = default;
    ~AgentLink();

    // Owns the fd; copying would double-close (same rationale as QmpClient).
    AgentLink(const AgentLink&) = delete;
    AgentLink& operator=(const AgentLink&) = delete;

    // Connects to the unix socket. Any banner line the guest wrote after this
    // client connected is consumed lazily by request() (a line that is not
    // the reply to the just-sent request - i.e. arrives before it - cannot
    // happen in the strict protocol, but the initial banner can; request()
    // skips one leading "esprite-agent ..." line if present).
    bool connect_unix(const std::string& socket_path, int timeout_ms, std::string* err);

    bool request(const std::string& line, std::string* reply, std::string* err,
                 int timeout_ms = 3000);

    void close();
    bool connected() const { return fd >= 0; }

    int fd = -1;
    std::string rxbuf;   // partial-line carry
};
