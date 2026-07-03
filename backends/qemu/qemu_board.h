#pragma once
#include <string>

struct SimTarget;

// A qemu board spec parsed from JSON (targets/qemu/*.json embedded at build
// time, or a user file via ESPRITE_QEMU_BOARD). Emulator targets are data,
// not code: onboarding a qemu board is a JSON file, never a board.cpp.
struct QemuBoardSpec {
    std::string key;          // target id, e.g. "qemu_esp32c3_rgb"
    std::string name;         // human hardware name (defaults to key)
    std::string description;  // list-targets description
    std::string machine;      // qemu -machine value, e.g. "esp32c3"
    std::string arch;         // "riscv32" | "xtensa" (selects ESPRITE_QEMU_<ARCH>)
    int display_w = 0;        // 0 = no display (tier 1, serial only)
    int display_h = 0;
};

// Pure parse + validation; no registration, so it is unit-testable without
// touching the process-global target registry.
bool qemu_board_parse(const std::string& json, QemuBoardSpec* spec, std::string* err);

// Allocates process-lifetime storage for the spec and registers it in the
// target registry. Rejects duplicate keys. Returns the registered target or
// null (with *err filled).
const SimTarget* qemu_board_register(const QemuBoardSpec& spec, std::string* err);

// Parses and registers one JSON board file from disk (the ESPRITE_QEMU_BOARD
// hook). Split out from qemu_boards_install so the file path is testable.
bool qemu_board_register_file(const std::string& path, std::string* err);

// Registers the built-in specs embedded from targets/qemu/*.json, then the
// optional ESPRITE_QEMU_BOARD file. Idempotent (second call is a no-op) so
// tests that enter esprite_main repeatedly do not double-register.
bool qemu_boards_install(std::string* err);
