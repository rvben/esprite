cmake_minimum_required(VERSION 3.20)
project(esp32sim CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Read-only path to the Clawdmeter firmware source (first onboarded target).
set(CLAWDMETER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../waveshare/Clawdmeter/firmware/src"
    CACHE PATH "Clawdmeter firmware src tree")

include(FetchContent)

# Vendored single-header libraries.
add_library(third_party INTERFACE)
target_include_directories(third_party INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Unit-test executable. Grows as components are added; Phase A is the smoke test.
add_executable(sim_tests
    tests/test_smoke.cpp
)
target_link_libraries(sim_tests PRIVATE third_party)

enable_testing()
add_test(NAME sim_tests COMMAND sim_tests)
