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

# Simulated firmware sources. The firmware evolves in its own repo; when it adds
# a file, the drift check below turns the otherwise-cryptic linker error into a
# configure-time warning naming the file.
set(CLAWDMETER_SIM_SOURCES
    main.cpp
    app_wifi.cpp
    wifi_link.cpp
    webconfig.cpp
    ota.cpp
    ui.cpp
    splash.cpp
    idle.cpp
    usage_rate.cpp
    limits_projection.cpp
    brightness.cpp
)

# Firmware sources deliberately NOT simulated (no BLE/audio shims; the sim's
# data path is HTTP). A new firmware file belongs in exactly one of these lists.
set(CLAWDMETER_UNSIMULATED_SOURCES
    app_buddy.cpp
    ble.cpp
    chime.cpp
    protocol.cpp
    es8311.c
)

file(GLOB _cm_present CONFIGURE_DEPENDS ${CM}/*.cpp ${CM}/*.c)
foreach(_f ${_cm_present})
    get_filename_component(_name ${_f} NAME)
    if(NOT _name IN_LIST CLAWDMETER_SIM_SOURCES
       AND NOT _name IN_LIST CLAWDMETER_UNSIMULATED_SOURCES
       AND NOT _name MATCHES "^font_.*\\.c$")
        message(WARNING "Clawdmeter gained ${_name}; add it to CLAWDMETER_SIM_SOURCES "
                        "(links against the shims) or CLAWDMETER_UNSIMULATED_SOURCES "
                        "in firmwares/clawdmeter/CMakeLists.txt")
    endif()
endforeach()

list(TRANSFORM CLAWDMETER_SIM_SOURCES PREPEND ${CM}/)

add_library(clawdmeter_fw STATIC
    ${CLAWDMETER_SIM_SOURCES}
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
