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

TEST_CASE("injected input is drained by read/available") {
    sim_serial_clear();
    sim_serial_inject("screenshot\n");
    std::string got;
    while (Serial.available()) got += (char)Serial.read();
    CHECK(got == "screenshot\n");
    CHECK(Serial.read() == -1);
}
