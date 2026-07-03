cmake_minimum_required(VERSION 3.20)
project(esprite VERSION 0.4.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Optimized by default; the Makefile's dev loop passes Debug explicitly.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Warnings for the sim's own code. Never applied to third-party builds (LVGL)
# or to the read-only firmware sources compiled into <fw>_fw libraries.
function(esprite_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()

# Read-only path to the agentgauge firmware source (first onboarded target).
set(AGENTGAUGE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../agentgauge/firmware/src"
    CACHE PATH "agentgauge firmware src tree")

include(FetchContent)

# Vendored single-header libraries. SYSTEM keeps -Wall/-Wextra scoped to the
# sim's own code.
add_library(third_party INTERFACE)
target_include_directories(third_party SYSTEM INTERFACE
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
esprite_warnings(shims)

# Core runtime + target registry. Declared before peripherals (below), which
# links against it.
add_library(core STATIC
    core/target.cpp
    core/runtime.cpp
    core/backend.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(core PUBLIC shims)
esprite_warnings(core)

# Virtual peripherals (display framebuffer, PNG screenshot, injected input bus,
# generic Arduino_GFX-to-framebuffer shim).
add_library(peripherals STATIC
    peripherals/display/framebuffer.cpp
    peripherals/display/screenshot.cpp
    peripherals/display/window_layout.cpp
    peripherals/input/sim_input.cpp
    peripherals/ble/sim_ble.cpp
    peripherals/gfx/arduino_gfx.cpp
    peripherals/tft_espi/tft_espi.cpp
)
target_include_directories(peripherals PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/display
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/input
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/ble
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/gfx
    ${CMAKE_CURRENT_SOURCE_DIR}/peripherals/tft_espi)
# sim_input.cpp and sim_ble.cpp register a core/runtime.cpp boot hook
# (sim_on_boot) to reset their state every sim_boot. That is a real link
# dependency on core that was missing here; declaring it (rather than relying
# on core happening to already be pulled in by something else in the same
# link) is what lets GNU ld place core's archive after peripherals's, since
# ld resolves archives in a single left-to-right pass and never revisits one
# it already scanned. Executables that never call sim_boot() directly (e.g.
# sim_itests_qemu, which only drives the QEMU backend) had nothing else pull
# in core/runtime.cpp.o before peripherals needed its symbol, so the link
# failed with GNU ld while ld64 (macOS) tolerated the missing edge.
target_link_libraries(peripherals PUBLIC third_party core)
esprite_warnings(peripherals)

# LVGL built from source with the sim lv_conf.h. Pinned to 9.5.0 for a stable
# core API surface (lv_font_t has static_bitmap).
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
esprite_warnings(lvgl_glue)

# ArduinoJson (used by the CLI scenario runner and by the agentgauge target).
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
# board.cpp (board profile + registration). Multiple variants of the same
# firmware can coexist in the single multi-target binary this way, each
# selecting its own panel/buttons at runtime via board_caps() from the active
# target's BoardDesc (agentgauge has just the one board today).
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
foreach(_fw agentgauge)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/firmwares/${_fw}/CMakeLists.txt)
        add_subdirectory(firmwares/${_fw})
    endif()
endforeach()

# Onboarded targets: board variants (which run a shared firmware) and standalone
# apps. Each provides its own CMakeLists via add_subdirectory.
foreach(_t waveshare_amoled_18 sample_gfx cyd cyd_tft qemu_esp32c3)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/targets/${_t}/CMakeLists.txt)
        add_subdirectory(targets/${_t})
    endif()
endforeach()

# QEMU backend: a minimal blocking QMP client over a unix socket, a process
# manager on top of it, and the SimBackend implementation that wires it into
# core's backend registry so BACKEND_QEMU targets boot a real QEMU child.
add_library(qemu_backend STATIC
    backends/qemu/qmp.cpp
    backends/qemu/qemu_process.cpp
    backends/qemu/qemu_backend.cpp
)
target_include_directories(qemu_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends/qemu)
# ArduinoJson's optional Arduino String integration (ARDUINOJSON_ENABLE_ARDUINO_STRING,
# set globally on the ArduinoJson target) pulls in Arduino.h even though qmp.cpp
# never calls into it; the declarations-only shim header satisfies that without
# linking the compiled shims library this library otherwise has no need for.
target_include_directories(qemu_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shims/arduino)
# core: qemu_backend.cpp implements SimBackend and calls sim_backend_register
# (core/backend.h, core/target.h). Core itself never links back against qemu
# code, so the registration hook keeps that layering one-directional.
target_link_libraries(qemu_backend PUBLIC ArduinoJson core)
esprite_warnings(qemu_backend)

# Shared CLI implementation, linked by both the binary and the test runner.
add_library(cli_lib STATIC cli/cli.cpp cli/scenario.cpp cli/actions.cpp cli/ble_bridge.cpp)
target_include_directories(cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cli)
target_link_libraries(cli_lib PUBLIC core peripherals lvgl_glue ArduinoJson qemu_backend)
target_compile_definitions(cli_lib PRIVATE ESPRITE_VERSION="${PROJECT_VERSION}")
esprite_warnings(cli_lib)
if(SDL2_FOUND)
    target_link_libraries(cli_lib PUBLIC sim_window)   # propagates HAVE_SDL2
endif()

add_executable(esprite cli/main.cpp)
target_link_libraries(esprite PRIVATE cli_lib)
link_sim_targets(esprite)

install(TARGETS esprite RUNTIME DESTINATION bin)

# Unit-test executable. Grows as components are added.
add_executable(sim_tests
    tests/test_smoke.cpp
    tests/test_arduino.cpp
    tests/test_serial.cpp
    tests/test_heapcaps.cpp
    tests/test_prefs.cpp
    tests/test_webserver.cpp
    tests/test_wifimanager.cpp
    tests/test_ble.cpp
    tests/test_framebuffer.cpp
    tests/test_screenshot.cpp
    tests/test_window_layout.cpp
    tests/test_target.cpp
    tests/test_lvgl_smoke.cpp
    tests/test_gfx.cpp
    tests/test_tft_espi.cpp
    tests/test_cli.cpp
    tests/test_daemon.cpp
    tests/test_backend.cpp
    tests/test_qmp.cpp
    tests/test_qemu_process.cpp
)
target_link_libraries(sim_tests PRIVATE cli_lib)
link_sim_targets(sim_tests)

# test_qmp.cpp runs a fake QMP server on a std::thread; pthreads is implicit
# via libc++ on Apple but must be linked explicitly on Linux/glibc.
find_package(Threads REQUIRED)
target_link_libraries(sim_tests PRIVATE Threads::Threads)

# Integration tests that boot real targets, isolated from unit tests so LVGL
# global state stays clean.
add_executable(sim_itests
    tests/itest_main.cpp
    tests/itest_sample_gfx.cpp
)
target_link_libraries(sim_itests PRIVATE cli_lib)
link_sim_targets(sim_itests)

# The agentgauge firmware is only built when its source is present (see
# firmwares/agentgauge/CMakeLists.txt). Only then compile the integration tests
# that boot it, so a clean checkout without the firmware still passes `make test`.
if(TARGET agentgauge_fw)
    # The run-session daemon driving an LVGL target end to end: lv_init runs
    # once per process, so it gets its own executable to keep LVGL global
    # state clean.
    add_executable(sim_itests_daemon tests/itest_main.cpp tests/itest_daemon.cpp)
    target_link_libraries(sim_itests_daemon PRIVATE cli_lib)
    link_sim_targets(sim_itests_daemon)

    # Scenario runner end to end through esprite_main (boots the LVGL firmware).
    add_executable(sim_itests_scenario tests/itest_main.cpp tests/itest_scenario.cpp)
    target_link_libraries(sim_itests_scenario PRIVATE cli_lib)
    link_sim_targets(sim_itests_scenario)

    # `expect` assertions end to end (own executable: one LVGL boot per process).
    add_executable(sim_itests_scenario_expect tests/itest_main.cpp tests/itest_scenario_expect.cpp)
    target_link_libraries(sim_itests_scenario_expect PRIVATE cli_lib)
    link_sim_targets(sim_itests_scenario_expect)

    # One-shot ui + bounded-array slicing over a live widget tree.
    add_executable(sim_itests_ui tests/itest_main.cpp tests/itest_ui.cpp)
    target_link_libraries(sim_itests_ui PRIVATE cli_lib)
    link_sim_targets(sim_itests_ui)
endif()

# Gated QEMU emulator integration tests: drive QemuProcess/QmpClient directly
# against real qemu-system-riscv32/-xtensa children. Always built (no target
# gate needed, unlike the LVGL-firmware executables above), but every
# TEST_CASE self-skips via MESSAGE + return when the QEMU env (.qemu/env.sh)
# or fixture images (tests/fixtures/qemu/) are absent, so `make test` stays
# green on a checkout that never ran `make qemu-fetch`/`qemu-fixtures`.
add_executable(sim_itests_qemu tests/itest_main.cpp tests/itest_qemu.cpp)
target_link_libraries(sim_itests_qemu PRIVATE cli_lib)
link_sim_targets(sim_itests_qemu)

enable_testing()
add_test(NAME sim_tests COMMAND sim_tests)
add_test(NAME sim_itests COMMAND sim_itests)
if(TARGET sim_itests_daemon)
    add_test(NAME sim_itests_daemon COMMAND sim_itests_daemon)
    add_test(NAME sim_itests_scenario COMMAND sim_itests_scenario)
    add_test(NAME sim_itests_scenario_expect COMMAND sim_itests_scenario_expect)
    add_test(NAME sim_itests_ui COMMAND sim_itests_ui)
endif()
add_test(NAME sim_itests_qemu COMMAND sim_itests_qemu)
