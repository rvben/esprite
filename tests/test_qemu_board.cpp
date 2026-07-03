#include "doctest.h"
#include "qemu_board.h"
#include <string>

TEST_CASE("qemu_board_parse reads a tier-1 spec (no display)") {
    QemuBoardSpec s;
    std::string err;
    REQUIRE_MESSAGE(qemu_board_parse(R"json({
        "key": "qemu_esp32c3",
        "name": "QEMU ESP32-C3 (tier 1: serial only)",
        "description": "Real flash image on Espressif QEMU",
        "machine": "esp32c3",
        "arch": "riscv32"
    })json", &s, &err), err);
    CHECK(s.key == "qemu_esp32c3");
    CHECK(s.machine == "esp32c3");
    CHECK(s.arch == "riscv32");
    CHECK(s.display_w == 0);
    CHECK(s.display_h == 0);
}

TEST_CASE("qemu_board_parse reads a display spec and defaults name to key") {
    QemuBoardSpec s;
    std::string err;
    REQUIRE_MESSAGE(qemu_board_parse(R"json({
        "key": "qemu_esp32c3_rgb",
        "description": "with virtual RGB panel",
        "machine": "esp32c3",
        "arch": "riscv32",
        "display": {"width": 320, "height": 240}
    })json", &s, &err), err);
    CHECK(s.name == "qemu_esp32c3_rgb");
    CHECK(s.display_w == 320);
    CHECK(s.display_h == 240);
}

TEST_CASE("qemu_board_parse rejects bad specs") {
    QemuBoardSpec s;
    std::string err;
    CHECK_FALSE(qemu_board_parse("not json", &s, &err));
    CHECK_FALSE(qemu_board_parse(R"json({"key":"","description":"d","machine":"m","arch":"riscv32"})json", &s, &err));
    CHECK_FALSE(qemu_board_parse(R"json({"key":"k","description":"d","arch":"riscv32"})json", &s, &err));       // no machine
    CHECK_FALSE(qemu_board_parse(R"json({"key":"k","description":"d","machine":"m","arch":"mips"})json", &s, &err));  // unknown arch
    CHECK(err.find("arch") != std::string::npos);
    CHECK_FALSE(qemu_board_parse(R"json({"key":"k","description":"d","machine":"m","arch":"riscv32",
        "display":{"width":0,"height":240}})json", &s, &err));                                              // bad dims
    CHECK_FALSE(qemu_board_parse(R"json({"key":"k","description":"d","machine":"m","arch":"riscv32",
        "display":{"width":320}})json", &s, &err));                                                         // half a display
}

#include "cli_test_helpers.h"
#include "target.h"
#include <cstdio>
#include <cstdlib>

TEST_CASE("built-in qemu board specs register both C3 targets") {
    std::string out;
    CHECK(run_cli_out({"esprite", "list-targets", "--limit", "50"}, &out) == 0);
    CHECK(out.find("\"key\":\"qemu_esp32c3\"") != std::string::npos);
    CHECK(out.find("\"key\":\"qemu_esp32c3_rgb\",\"name\":\"QEMU ESP32-C3 + virtual RGB panel\",\"width\":320,\"height\":240") != std::string::npos);
}

TEST_CASE("qemu_board_register rejects a duplicate key") {
    QemuBoardSpec s;
    std::string err;
    REQUIRE(qemu_board_parse(R"json({"key":"test_qemu_board_dup","description":"d","machine":"esp32c3","arch":"riscv32"})json", &s, &err));
    CHECK(qemu_board_register(s, &err) != nullptr);
    CHECK(qemu_board_register(s, &err) == nullptr);
    CHECK(err.find("duplicate") != std::string::npos);
}

TEST_CASE("ESPRITE_QEMU_BOARD registers a user board file") {
    std::string dir = "/tmp/esprite_test_qemu_board";
    system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    std::string path = dir + "/b.json";
    FILE* f = fopen(path.c_str(), "w");
    REQUIRE(f);
    fputs(R"json({"key":"test_qemu_board_user","description":"user board","machine":"esp32c3","arch":"riscv32","display":{"width":64,"height":32}})json", f);
    fclose(f);
    std::string err;
    CHECK_MESSAGE(qemu_board_register_file(path, &err), err);
    const SimTarget* t = sim_target("test_qemu_board_user");
    REQUIRE(t != nullptr);
    CHECK(t->backend == BACKEND_QEMU);
    CHECK(t->board->width == 64);
    CHECK_FALSE(qemu_board_register_file(dir + "/missing.json", &err));
}
