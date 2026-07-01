# The Clawdmeter firmware (a Claude Code token-meter desk buddy), compiled once
# from source against the sim shims into clawdmeter_fw. It is board-agnostic --
# it renders itself from board_caps() at runtime -- so the board targets under
# targets/ (waveshare_amoled_216_c6, waveshare_amoled_18) select the panel and
# share this one library. Firmware files are referenced read-only from
# CLAWDMETER_SRC and never modified.

set(CM ${CLAWDMETER_SRC})

if(NOT EXISTS ${CM}/main.cpp)
    message(WARNING "Clawdmeter source not found at ${CM}; skipping clawdmeter firmware")
    return()
endif()

add_library(clawdmeter_fw STATIC
    ${CM}/main.cpp
    ${CM}/app_wifi.cpp
    ${CM}/wifi_link.cpp
    ${CM}/webconfig.cpp
    ${CM}/ota.cpp
    ${CM}/ui.cpp
    ${CM}/splash.cpp
    ${CM}/idle.cpp
    ${CM}/usage_rate.cpp
    ${CM}/brightness.cpp
    # Pre-compiled LVGL fonts.
    ${CM}/font_mono_18.c
    ${CM}/font_mono_32.c
    ${CM}/font_styrene_12.c
    ${CM}/font_styrene_14.c
    ${CM}/font_styrene_16.c
    ${CM}/font_styrene_20.c
    ${CM}/font_styrene_24.c
    ${CM}/font_styrene_28.c
    ${CM}/font_styrene_48.c
    ${CM}/font_tiempos_34.c
    ${CM}/font_tiempos_56.c
    # Generic sim board adapter (firmware HAL bound to the virtual peripherals).
    # board_caps() is runtime-selected from the active target, so this serves
    # every board variant.
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/board_caps.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/board_init.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/display.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/input.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/touch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/power.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/imu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/sound.cpp
)

# Rename the Arduino entry points so this firmware's setup()/loop() do not clash
# with other firmwares (e.g. sample_gfx) in the same binary. Applied only to
# main.cpp; every board variant registers these same shared entries.
set_source_files_properties(${CM}/main.cpp
    TARGET_DIRECTORY clawdmeter_fw
    PROPERTIES COMPILE_DEFINITIONS "setup=clawdmeter_setup;loop=clawdmeter_loop")

target_include_directories(clawdmeter_fw PUBLIC
    ${CM}
    ${CMAKE_CURRENT_LIST_DIR}/board_sim)
target_link_libraries(clawdmeter_fw PUBLIC core peripherals lvgl_glue ArduinoJson)
