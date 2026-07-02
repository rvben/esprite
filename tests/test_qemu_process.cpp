#include "doctest.h"
#include "qemu_process.h"
#include <unistd.h>
#include <string>
#include <vector>

TEST_CASE("qemu argv is exact with icount") {
    QemuSpec s{"/q/qemu-system-riscv32", "esp32c3", "/f/flash.bin", true, "/tmp/q.sock"};
    auto v = qemu_build_argv(s);
    std::vector<std::string> want = {
        "/q/qemu-system-riscv32", "-machine", "esp32c3", "-nographic",
        "-drive", "file=/f/flash.bin,if=mtd,format=raw",
        "-qmp", "unix:/tmp/q.sock,server=on,wait=off",
        "-icount", "shift=3,align=off,sleep=off"};
    CHECK(v == want);
}

TEST_CASE("qemu argv omits -icount when spec.icount is false") {
    QemuSpec s{"/q/qemu-system-riscv32", "esp32c3", "/f/flash.bin", false, "/tmp/q.sock"};
    auto v = qemu_build_argv(s);
    std::vector<std::string> want = {
        "/q/qemu-system-riscv32", "-machine", "esp32c3", "-nographic",
        "-drive", "file=/f/flash.bin,if=mtd,format=raw",
        "-qmp", "unix:/tmp/q.sock,server=on,wait=off"};
    CHECK(v == want);
}

TEST_CASE("child lifecycle: spawn, capture stdout, orderly stop") {
    // Uses /bin/cat with spawn_only() (spawn without QMP connect) so the
    // process plumbing is tested without QEMU. echo via stdin -> stdout.
    QemuProcess p;
    std::string err;
    REQUIRE(p.spawn_only({"/bin/cat"}, &err));
    CHECK(p.running());
    CHECK(p.serial_write("hello\n"));
    for (int i = 0; i < 100 && p.serial_output().empty(); ++i) { p.pump(); usleep(10000); }
    CHECK(p.serial_output() == "hello\n");
    p.stop();
    CHECK(!p.running());
}

TEST_CASE("stop before start is a safe no-op") {
    QemuProcess p;
    p.stop();
    CHECK(!p.running());
}

TEST_CASE("stop is idempotent: safe to call twice after a real spawn") {
    QemuProcess p;
    std::string err;
    REQUIRE(p.spawn_only({"/bin/cat"}, &err));
    p.stop();
    CHECK(!p.running());
    p.stop();   // second stop after the child is already reaped: must not hang or crash
    CHECK(!p.running());
}

TEST_CASE("spawn_only reports an error for a nonexistent binary") {
    QemuProcess p;
    std::string err;
    CHECK(!p.spawn_only({"/nonexistent/binary-does-not-exist"}, &err));
    CHECK(!err.empty());
    CHECK(!p.running());
}
