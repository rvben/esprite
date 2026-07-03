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
