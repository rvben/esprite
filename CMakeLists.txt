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

# Reusable Arduino + ESP-IDF shims (chip- and app-agnostic).
add_library(shims STATIC
    shims/arduino/arduino_clock.cpp
    shims/arduino/serial.cpp
    shims/esp/preferences.cpp
    shims/net/webserver.cpp
)
target_include_directories(shims PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shims/arduino
    ${CMAKE_CURRENT_SOURCE_DIR}/shims/esp
    ${CMAKE_CURRENT_SOURCE_DIR}/shims/net)
target_link_libraries(shims PUBLIC third_party)

# Virtual peripherals (display framebuffer + PNG screenshot).
add_library(peripherals STATIC
    peripherals/display/framebuffer.cpp
    peripherals/display/screenshot.cpp
)
target_include_directories(peripherals PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/display)
target_link_libraries(peripherals PUBLIC third_party)

# Core runtime + target registry.
add_library(core STATIC
    core/target.cpp
    core/runtime.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(core PUBLIC shims)

# LVGL 9.2 built from source with the sim lv_conf.h.
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.2.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/lvgl)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# LVGL globs its NEON .S sources unconditionally. We use the NONE asm path, and
# Apple's assembler cannot process those files (they pull in C headers), so drop
# every .S source from the target.
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# LVGL-to-framebuffer glue for LVGL-based targets.
add_library(lvgl_glue STATIC peripherals/lvgl/lvgl_display.cpp)
target_include_directories(lvgl_glue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/lvgl)
target_link_libraries(lvgl_glue PUBLIC lvgl peripherals)

# Unit-test executable. Grows as components are added.
add_executable(sim_tests
    tests/test_smoke.cpp
    tests/test_arduino.cpp
    tests/test_serial.cpp
    tests/test_heapcaps.cpp
    tests/test_prefs.cpp
    tests/test_webserver.cpp
    tests/test_framebuffer.cpp
    tests/test_screenshot.cpp
    tests/test_target.cpp
    tests/test_lvgl_smoke.cpp
)
target_link_libraries(sim_tests PRIVATE core lvgl_glue)

enable_testing()
add_test(NAME sim_tests COMMAND sim_tests)
