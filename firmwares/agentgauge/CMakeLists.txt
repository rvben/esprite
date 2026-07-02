# The agentgauge firmware (a Wi-Fi AI coding-agent rate-limit desk gauge),
# compiled once from source against the sim shims into agentgauge_fw. There is
# one board (480x480 AMOLED, two side buttons + PWR), so this firmware is not
# runtime board_caps()-switched across variants; the single targets/agentgauge
# board links it directly. Firmware files are referenced read-only from
# AGENTGAUGE_SRC and never modified.

set(AG ${AGENTGAUGE_SRC})

if(NOT EXISTS ${AG}/main.cpp)
    message(WARNING "agentgauge source not found at ${AG}; skipping agentgauge firmware")
    return()
endif()

# Simulated firmware sources: the app layer (Wi-Fi transport, config page, OTA,
# UI, boot glue). The firmware evolves in its own repo; when it adds a file,
# the drift check below turns the otherwise-cryptic linker error into a
# configure-time warning naming the file.
set(AGENTGAUGE_SIM_SOURCES
    main.cpp
    app_wifi.cpp
    wifi_link.cpp
    webconfig.cpp
    ota.cpp
    ui.cpp
    brightness.cpp
    idle.cpp
)

# Firmware sources deliberately NOT simulated: board.cpp does real I2C/PMU/IMU
# bring-up (Wire, AXP2101, QMI8658, the CO5300 panel) that board_sim/board_init.cpp
# replaces wholesale with shim init calls. hal/*.cpp (the real Arduino_GFX /
# TouchDrvCST92xx / XPowersLib drivers behind hal/*.h) are likewise never built --
# board_sim/*.cpp implements those same header contracts against esprite's
# virtual peripherals instead. A new firmware file belongs in exactly one of
# these two lists.
set(AGENTGAUGE_UNSIMULATED_SOURCES
    board.cpp
)

file(GLOB _ag_present CONFIGURE_DEPENDS ${AG}/*.cpp)
foreach(_f ${_ag_present})
    get_filename_component(_name ${_f} NAME)
    if(NOT _name IN_LIST AGENTGAUGE_SIM_SOURCES
       AND NOT _name IN_LIST AGENTGAUGE_UNSIMULATED_SOURCES)
        message(WARNING "agentgauge gained ${_name}; add it to AGENTGAUGE_SIM_SOURCES "
                        "or AGENTGAUGE_UNSIMULATED_SOURCES "
                        "in firmwares/agentgauge/CMakeLists.txt")
    endif()
endforeach()

# Tolerate version skew in the other direction too: a listed file missing from
# an older firmware checkout drops out instead of failing configure.
foreach(_f ${AGENTGAUGE_SIM_SOURCES})
    if(NOT EXISTS ${AG}/${_f})
        message(STATUS "agentgauge at ${AG} predates ${_f}; building without it")
        list(REMOVE_ITEM AGENTGAUGE_SIM_SOURCES ${_f})
    endif()
endforeach()

list(TRANSFORM AGENTGAUGE_SIM_SOURCES PREPEND ${AG}/)

add_library(agentgauge_fw STATIC
    ${AGENTGAUGE_SIM_SOURCES}
    # Inter LVGL font sources (lv_font_conv output, not globbed above since
    # the drift check only watches *.cpp - see AGENTGAUGE_SIM_SOURCES).
    ${AG}/font_inter_16.c
    ${AG}/font_inter_24.c
    ${AG}/font_inter_48.c
    # Sim board adapter: the firmware's hal/*.h contract bound to the virtual
    # peripherals (framebuffer, injected touch/button/battery state).
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/board_caps.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/board_init.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/display.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/input.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/touch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board_sim/power.cpp
)

# Rename the Arduino entry points so this firmware's setup()/loop() do not
# clash with other firmwares (e.g. sample_gfx) in the same binary.
set_source_files_properties(${AG}/main.cpp
    TARGET_DIRECTORY agentgauge_fw
    PROPERTIES COMPILE_DEFINITIONS "setup=agentgauge_setup;loop=agentgauge_loop")

target_include_directories(agentgauge_fw PUBLIC
    ${AG}
    ${CMAKE_CURRENT_LIST_DIR}/board_sim)
target_link_libraries(agentgauge_fw PUBLIC core peripherals lvgl_glue ArduinoJson)
