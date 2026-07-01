#include "doctest.h"
#include "Arduino.h"
#include <string>

TEST_CASE("virtual clock starts at zero and advances with delay") {
    sim_clock_reset();
    CHECK(millis() == 0);
    delay(10);
    CHECK(millis() >= 10);
    sim_clock_advance(5);
    CHECK(millis() >= 15);
}

TEST_CASE("map scales linearly like Arduino") {
    CHECK(map(5, 0, 10, 0, 100) == 50);
    CHECK(map(0, 0, 10, 0, 100) == 0);
    CHECK(map(10, 0, 10, 0, 100) == 100);
}

TEST_CASE("constrain clamps") {
    CHECK(constrain(5, 0, 10) == 5);
    CHECK(constrain(-1, 0, 10) == 0);
    CHECK(constrain(11, 0, 10) == 10);
}

TEST_CASE("String exposes c_str and length") {
    String s("hi");
    CHECK(std::string(s.c_str()) == "hi");
    CHECK(s.length() == 2);
    String n(42);
    CHECK(std::string(n.c_str()) == "42");
}
