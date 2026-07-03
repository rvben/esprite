#pragma once
// Helpers for driving the real CLI entry point (esprite_main) from tests and
// capturing its stdout/stderr, so assertions cover exit codes, error kinds,
// and result payloads through the exact production code path.
#include "doctest.h"
#include "cli.h"
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>
#include <unistd.h>

inline int run_cli(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    static std::vector<std::string> storage;
    storage.clear();
    for (auto* a : args) storage.emplace_back(a);
    for (auto& s : storage) argv.push_back(const_cast<char*>(s.c_str()));
    return esprite_main((int)argv.size(), argv.data());
}

inline int run_cli_capture(std::initializer_list<const char*> args, FILE* stream, std::string* text) {
    fflush(stream);
    int saved = dup(fileno(stream));
    FILE* tmp = tmpfile();
    REQUIRE(tmp != nullptr);
    dup2(fileno(tmp), fileno(stream));
    int rc = run_cli(args);
    fflush(stream);
    dup2(saved, fileno(stream));
    close(saved);
    rewind(tmp);
    char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    fclose(tmp);
    text->assign(buf, n);
    return rc;
}

// Capture stderr: the error envelope, so tests assert the error *kind*.
inline int run_cli_err(std::initializer_list<const char*> args, std::string* err) {
    return run_cli_capture(args, stderr, err);
}

// Capture stdout: the result payload.
inline int run_cli_out(std::initializer_list<const char*> args, std::string* out) {
    return run_cli_capture(args, stdout, out);
}

// Drive one full `run` session (esprite_daemon) in-process: newline-delimited
// JSON commands in, the concatenated reply lines back. In-process matters:
// tests can inspect sim state (e.g. sim_framebuffer()) the session left
// behind after it returns.
inline std::string run_daemon(const std::string& input) {
    FILE* in = fmemopen((void*)input.data(), input.size(), "r");
    char* buf = nullptr;
    size_t len = 0;
    FILE* out = open_memstream(&buf, &len);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    esprite_daemon(in, out);
    fclose(in);
    fclose(out);
    std::string reply(buf, len);
    free(buf);
    return reply;
}
