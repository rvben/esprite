#include "doctest.h"
#include "qemu_process.h"
#include <unistd.h>
#include <chrono>
#include <string>
#include <vector>

TEST_CASE("qemu argv is exact with icount") {
    QemuSpec s{"/q/qemu-system-riscv32", "esp32c3", "/f/flash.bin", true, "/tmp/q.sock"};
    auto v = qemu_build_argv(s);
    std::vector<std::string> want = {
        "/q/qemu-system-riscv32", "-machine", "esp32c3", "-nographic",
        "-drive", "file=/f/flash.bin,if=mtd,format=raw,snapshot=on",
        "-qmp", "unix:/tmp/q.sock,server=on,wait=off",
        "-icount", "shift=3,align=off,sleep=off"};
    CHECK(v == want);
}

TEST_CASE("qemu argv omits -icount when spec.icount is false") {
    QemuSpec s{"/q/qemu-system-riscv32", "esp32c3", "/f/flash.bin", false, "/tmp/q.sock"};
    auto v = qemu_build_argv(s);
    std::vector<std::string> want = {
        "/q/qemu-system-riscv32", "-machine", "esp32c3", "-nographic",
        "-drive", "file=/f/flash.bin,if=mtd,format=raw,snapshot=on",
        "-qmp", "unix:/tmp/q.sock,server=on,wait=off"};
    CHECK(v == want);
}

TEST_CASE("qemu argv wires the agent chardev to UART1 and keeps the console on stdio") {
    QemuSpec s{"/q/qemu-system-riscv32", "esp32c3", "/f/flash.bin", false, "/tmp/q.sock"};
    s.agent_socket = "/tmp/agent.sock";
    auto v = qemu_build_argv(s);
    // Explicit -serial flags replace -nographic's implicit assignment in
    // order: slot 0 must stay the stdio mux or the console goes dark.
    std::vector<std::string> want = {
        "/q/qemu-system-riscv32", "-machine", "esp32c3", "-nographic",
        "-serial", "mon:stdio",
        "-serial", "unix:/tmp/agent.sock,server=on,wait=off",
        "-drive", "file=/f/flash.bin,if=mtd,format=raw,snapshot=on",
        "-qmp", "unix:/tmp/q.sock,server=on,wait=off"};
    CHECK(v == want);
}

TEST_CASE("qemu argv adds user-net hostfwd for http-capable boards") {
    QemuSpec s{"/q/qemu-system-riscv32", "esp32c3", "/f/flash.bin", false, "/tmp/q.sock"};
    s.http_host_port = 18080;
    s.http_guest_port = 80;
    auto v = qemu_build_argv(s);
    std::vector<std::string> want = {
        "/q/qemu-system-riscv32", "-machine", "esp32c3", "-nographic",
        "-drive", "file=/f/flash.bin,if=mtd,format=raw,snapshot=on",
        "-qmp", "unix:/tmp/q.sock,server=on,wait=off",
        "-nic", "user,model=open_eth,hostfwd=tcp:127.0.0.1:18080-:80"};
    CHECK(v == want);
}

TEST_CASE("allocate_ephemeral_port returns a usable localhost port") {
    std::string err;
    int p = allocate_ephemeral_port(&err);
    CHECK_MESSAGE(p > 0, err);
    CHECK(p <= 65535);
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

TEST_CASE("serial_write returns false within the deadline against a child that never reads") {
    // /bin/sleep never touches its stdin, so the pipe fills (default macOS/
    // Linux pipe capacity is 64KB) and a naive blocking write() would wedge
    // forever once the buffer is full. A 128KB payload guarantees it fills:
    // the write loop must give up at kSerialWriteDeadlineMs (2000ms) rather
    // than hang, which is the actual regression this guards against.
    QemuProcess p;
    std::string err;
    REQUIRE(p.spawn_only({"/bin/sleep", "5"}, &err));
    std::string payload(128 * 1024, 'x');
    auto start = std::chrono::steady_clock::now();
    bool ok = p.serial_write(payload);
    auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(!ok);
    // Generous upper bound (well above the 2000ms deadline) so this stays
    // robust under slow/loaded CI without weakening what it actually checks:
    // that serial_write returns at all instead of hanging for the full 5s
    // the child stays alive.
    CHECK(elapsed < std::chrono::milliseconds(4000));
    p.stop();
    CHECK(!p.running());
}

TEST_CASE("qemu_needs_fd_normalize flags only 0/1/2") {
    // spawn_only() must move a pipe fd that lands on 0/1/2 (possible when the
    // parent starts with a stdio fd already closed, e.g. under a
    // daemon/supervisor) before referencing it by number in a
    // posix_spawn_file_actions_t; this is the pure decision that gates it.
    CHECK(qemu_needs_fd_normalize(0));
    CHECK(qemu_needs_fd_normalize(1));
    CHECK(qemu_needs_fd_normalize(2));
    CHECK(!qemu_needs_fd_normalize(3));
    CHECK(!qemu_needs_fd_normalize(4));
    CHECK(!qemu_needs_fd_normalize(1024));
    CHECK(!qemu_needs_fd_normalize(-1));
}
