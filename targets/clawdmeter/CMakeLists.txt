# Clawdmeter target: the waveshare_amoled_216_c6_wifi firmware, compiled from
# source against the sim shims. Firmware files are referenced read-only from
# CLAWDMETER_SRC and never modified.

set(CM ${CLAWDMETER_SRC})

if(NOT EXISTS ${CM}/main.cpp)
    message(WARNING "Clawdmeter source not found at ${CM}; skipping clawdmeter target")
    return()
endif()

add_sim_target(clawdmeter
    # Shared firmware set (the Wi-Fi env minus BLE and audio).
    ${CM}/main.cpp
    ${CM}/app_wifi.cpp
    ${CM}/wifi_link.cpp
    ${CM}/webconfig.cpp
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
    # Sim board adapter (Clawdmeter HAL bound to the virtual peripherals).
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/board_caps.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/board_init.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/display.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/input.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/touch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/power.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/imu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/sound.cpp
    # Target registration.
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp
)

# Rename the Arduino entry points so this target's setup()/loop() do not clash
# with other targets linked into the same binary. Applied only to main.cpp.
set_source_files_properties(${CM}/main.cpp
    TARGET_DIRECTORY target_clawdmeter
    PROPERTIES COMPILE_DEFINITIONS "setup=clawdmeter_setup;loop=clawdmeter_loop")

target_include_directories(target_clawdmeter PRIVATE
    ${CM}
    ${CMAKE_CURRENT_LIST_DIR}/board_sim)
