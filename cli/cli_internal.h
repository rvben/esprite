#pragma once
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <string>

// Helpers shared by the CLI dialect frontends (one-shot commands, the run
// session, the scenario runner) so the error envelope, exit codes, and value
// parsing can never drift between them.

// JSON-escape a string (for structured stdout/stderr results).
inline std::string json_esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}

inline const char* jbool(bool b) { return b ? "true" : "false"; }

// Structured error envelope as the last line of stderr; returns the exit code.
inline int fail(const char* kind, const std::string& msg, int code) {
    fprintf(stderr, "{\"error\":{\"kind\":\"%s\",\"message\":\"%s\"}}\n", kind, json_esc(msg).c_str());
    return code;
}

// The exit code the schema documents for an error kind.
inline int kind_exit(const std::string& kind) {
    if (kind == "ref_not_found") return 4;
    if (kind == "conflict")      return 5;
    if (kind == "post_failed")   return 6;
    if (kind == "unsupported")   return 7;
    if (kind == "bind_failed")   return 3;
    if (kind == "expect_failed") return 8;
    return 2;   // bad_args, no_target, unknown_target
}

// Parse a complete integer within [min,max]. Rejects garbage, trailing text,
// and out-of-range values, so no command ever acts on atoi()-style zeros.
inline bool to_long(const std::string& s, long min, long max, long* out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size() || v < min || v > max) return false;
    *out = v;
    return true;
}
