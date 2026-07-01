# CYD target (ESP32-2432S028R, "Cheap Yellow Display"): a 320x240 Arduino_GFX
# touch-paint demo. No custom HAL - it runs against the framework's GFX shim and
# the sim touch bus, showing esprite hosting a board unlike the Waveshare ones.

add_sim_target(cyd
    ${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp
)

# Rename the Arduino entry points so they do not clash with other targets.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    TARGET_DIRECTORY target_cyd
    PROPERTIES COMPILE_DEFINITIONS "setup=cyd_setup;loop=cyd_loop")
