#include "doctest.h"
#include "Arduino.h"
#include <string>

TEST_CASE("printf output is captured and regex-searchable") {
    sim_serial_clear();
    Serial.printf("Dashboard ready (%s, %dx%d)\n", "Sim", 480, 480);
    CHECK(sim_serial_contains("Dashboard ready"));
    CHECK(sim_serial_contains("480x480"));
    CHECK_FALSE(sim_serial_contains("nope"));
}

TEST_CASE("println returns the byte count it wrote, like Print on the device") {
    sim_serial_clear();
    CHECK(Serial.println(42) == 3);   // "42\n"
}

TEST_CASE("printf output beyond 1 KB is captured in full, not truncated") {
    // Real Print::printf sizes its buffer to the formatted length; the shim
    // truncated at a fixed 1024-byte stack buffer, so long debug dumps
    // differed between sim and device.
    sim_serial_clear();
    std::string big(2000, 'x');
    CHECK(Serial.printf("%s", big.c_str()) == 2000);
    CHECK(sim_serial_contains("x{2000}"));
}

TEST_CASE("injected input is drained by read/available") {
    sim_serial_clear();
    sim_serial_inject("screenshot\n");
    std::string got;
    while (Serial.available()) got += (char)Serial.read();
    CHECK(got == "screenshot\n");
    CHECK(Serial.read() == -1);
}
