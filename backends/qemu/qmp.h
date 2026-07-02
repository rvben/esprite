#pragma once
#include <string>

// Minimal QMP (QEMU Machine Protocol) client over a unix domain socket.
// Blocking with deadlines: every connect and read honors a poll()-based
// timeout, but the client itself never spawns threads. QMP is low-rate
// control traffic (negotiate, quit, later screendump), never the serve-loop
// hot path, so a bounded-blocking implementation stays simple without
// costing the sim anything.
struct QmpClient {
    QmpClient() = default;
    ~QmpClient();

    // Connects to socket_path, reads the {"QMP":...} greeting, sends
    // {"execute":"qmp_capabilities"} and waits for its {"return":{}} before
    // returning. On any failure (connect refused, greeting malformed,
    // capabilities negotiation errors or times out), closes the socket and
    // fills *err.
    bool connect_unix(const std::string& socket_path, int timeout_ms, std::string* err);

    // Sends {"execute":cmd,"arguments":<args_json>} (args_json == "" omits
    // "arguments" entirely) and waits for the matching reply, skipping any
    // interleaved {"event":...} lines. On {"return":...}, fills *result with
    // the raw return value re-serialized as JSON and returns true. On
    // {"error":{"desc":...}}, fills *err with the desc and returns false.
    bool execute(const std::string& cmd, const std::string& args_json,
                 std::string* result, std::string* err, int timeout_ms = 5000);

    // Closes the socket if open. Idempotent: safe to call on an already
    // closed (or never connected) client.
    void close();
    bool connected() const { return fd >= 0; }

    int fd = -1;
    std::string rxbuf;   // partial-line carry
};
