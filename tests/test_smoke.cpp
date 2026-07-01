#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("toolchain builds and runs") {
    CHECK(1 + 1 == 2);
}
