#pragma once
#include <cstdio>

// Entry point for the esprite CLI. Returns a process exit code.
int esprite_main(int argc, char** argv);

// The `run` session loop: newline-delimited JSON commands from `in`, one JSON
// reply per line on `out`. Exposed so tests can drive a session in-process.
int esprite_daemon(FILE* in, FILE* out);
