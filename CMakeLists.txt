cmake_minimum_required(VERSION 3.20)
project(esprite CXX C)

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

# Virtual peripherals (display framebuffer, PNG screenshot, injected input bus,
# generic Arduino_GFX-to-framebuffer shim).
add_library(peripherals STATIC
    peripherals/display/framebuffer.cpp
    peripherals/display/screenshot.cpp
    peripherals/input/sim_input.cpp
    peripherals/gfx/arduino_gfx.cpp
)
target_include_directories(peripherals PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/display
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/input
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/gfx)
target_link_libraries(peripherals PUBLIC third_party)

# Core runtime + target registry.
add_library(core STATIC
    core/target.cpp
    core/runtime.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(core PUBLIC shims)

# LVGL built from source with the sim lv_conf.h. Pinned to 9.5.0 to match the
# version the Clawdmeter fonts were patched for (lv_font_t has static_bitmap).
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/lvgl/lv_conf.h
    CACHE STRING "" FORCE)
# The sim uses only core LVGL: no demos, examples, or ThorVG vector graphics
# (lv_conf.h enables none of them). LVGL's desktop build defaults these ON and
# links each back to lvgl with a cyclic static dependency, so every one appears
# twice on the final link line ("ignoring duplicate libraries"). Disabling them
# removes that noise and drops ~2.9 MB of needless object code from the build.
# Set before MakeAvailable so LVGL's option() calls adopt these values.
set(CONFIG_LV_BUILD_DEMOS         OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)

# LVGL globs its NEON .S sources unconditionally. We use the NONE asm path, and
# Apple's assembler cannot process those files (they pull in C headers), so drop
# every .S source from the target.
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# LVGL-to-framebuffer glue for LVGL-based targets.
add_library(lvgl_glue STATIC
    peripherals/lvgl/lvgl_display.cpp
    peripherals/lvgl/lvgl_snapshot.cpp)
target_include_directories(lvgl_glue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/lvgl)
target_link_libraries(lvgl_glue PUBLIC lvgl peripherals)

# ArduinoJson (used by the CLI scenario runner and by the clawdmeter target).
FetchContent_Declare(ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.2.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ArduinoJson)
# Let ArduinoJson accept our Arduino String shim (uses c_str()/length()).
target_compile_definitions(ArduinoJson INTERFACE ARDUINOJSON_ENABLE_ARDUINO_STRING=1)

# Optional live device window (SDL2). Built only when SDL2 is present; headless
# builds and CI are unaffected. The CLI guards --window on HAVE_SDL2.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_library(sim_window STATIC peripherals/display/sim_window.cpp)
    target_include_directories(sim_window PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/display
        ${CMAKE_CURRENT_SOURCE_DIR}/core)   # SimButton in target.h
    target_link_libraries(sim_window PUBLIC peripherals)
    if(TARGET SDL2::SDL2)
        target_link_libraries(sim_window PUBLIC SDL2::SDL2)
    else()
        target_include_directories(sim_window PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(sim_window PUBLIC ${SDL2_LIBRARIES})
    endif()
    target_compile_definitions(sim_window PUBLIC HAVE_SDL2)
    message(STATUS "SDL2 ${SDL2_VERSION} found; live --window enabled")
else()
    message(STATUS "SDL2 not found; --window disabled (headless only)")
endif()

# Each onboarded target is a static library; a static initializer in its
# board.cpp self-registers it. Force-load so the linker keeps that object even
# though nothing references it directly.
define_property(GLOBAL PROPERTY SIM_TARGET_LIBS BRIEF_DOCS "x" FULL_DOCS "x")
function(add_sim_target name)
    add_library(target_${name} STATIC ${ARGN})
    target_link_libraries(target_${name} PUBLIC core peripherals lvgl_glue ArduinoJson)
    set_property(GLOBAL APPEND PROPERTY SIM_TARGET_LIBS target_${name})
endfunction()

# A board variant reuses an already-built firmware library and only adds its own
# board.cpp (board profile + registration). Multiple variants share one compiled
# firmware, so boards of the same firmware (e.g. the 480x480 C6 and the 368x448
# 1.8) coexist in the single multi-target binary. The board profile is selected
# at runtime by board_caps() from the active target's BoardDesc.
function(add_board_variant name fw)
    # Called from targets/<board>/CMakeLists.txt, so board.cpp resolves against
    # that board's source dir.
    add_library(target_${name} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/board.cpp)
    target_link_libraries(target_${name} PUBLIC ${fw})
    set_property(GLOBAL APPEND PROPERTY SIM_TARGET_LIBS target_${name})
endfunction()

function(link_sim_targets exe)
    get_property(_libs GLOBAL PROPERTY SIM_TARGET_LIBS)
    foreach(_lib ${_libs})
        if(APPLE)
            target_link_libraries(${exe} PRIVATE ${_lib})
            target_link_options(${exe} PRIVATE "-Wl,-force_load,$<TARGET_FILE:${_lib}>")
        else()
            # GNU ld / lld whole-archive keeps the target's self-registration.
            target_link_libraries(${exe} PRIVATE
                "-Wl,--whole-archive" ${_lib} "-Wl,--no-whole-archive")
        endif()
    endforeach()
endfunction()

# Shared firmwares, compiled once (a firmware is board-agnostic; board targets
# below select the panel). Added before the board targets that reference them.
foreach(_fw clawdmeter)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/firmwares/${_fw}/CMakeLists.txt)
        add_subdirectory(firmwares/${_fw})
    endif()
endforeach()

# Onboarded targets: board variants (which run a shared firmware) and standalone
# apps. Each provides its own CMakeLists via add_subdirectory.
foreach(_t waveshare_amoled_216_c6 waveshare_amoled_18 sample_gfx)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/targets/${_t}/CMakeLists.txt)
        add_subdirectory(targets/${_t})
    endif()
endforeach()

# Shared CLI implementation, linked by both the binary and the test runner.
add_library(cli_lib STATIC cli/cli.cpp cli/scenario.cpp)
target_include_directories(cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cli)
target_link_libraries(cli_lib PUBLIC core peripherals lvgl_glue ArduinoJson)
if(SDL2_FOUND)
    target_link_libraries(cli_lib PUBLIC sim_window)   # propagates HAVE_SDL2
endif()

add_executable(esprite cli/main.cpp)
target_link_libraries(esprite PRIVATE cli_lib)
link_sim_targets(esprite)

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
    tests/test_gfx.cpp
    tests/test_cli.cpp
)
target_link_libraries(sim_tests PRIVATE cli_lib)
link_sim_targets(sim_tests)

# Integration tests that boot real targets, isolated from unit tests so LVGL
# global state stays clean.
add_executable(sim_itests
    tests/itest_main.cpp
    tests/itest_sample_gfx.cpp
)
# The Clawdmeter firmware is only built when its source is present (see
# firmwares/clawdmeter/CMakeLists.txt). Only then compile the integration test
# that boots it, so a clean checkout without the firmware still passes `make test`.
if(TARGET clawdmeter_fw)
    target_sources(sim_itests PRIVATE tests/itest_clawdmeter.cpp)
endif()
target_link_libraries(sim_itests PRIVATE cli_lib)
link_sim_targets(sim_itests)

enable_testing()
add_test(NAME sim_tests COMMAND sim_tests)
add_test(NAME sim_itests COMMAND sim_itests)
