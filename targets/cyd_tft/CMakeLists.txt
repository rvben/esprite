# CYD target running a real TFT_eSPI sketch (the CYD community's dominant
# library), proving esprite can host off-the-shelf TFT_eSPI firmware.

add_sim_target(cyd_tft
    ${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp
)

# Rename the Arduino entry points so they do not clash with other targets.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    TARGET_DIRECTORY target_cyd_tft
    PROPERTIES COMPILE_DEFINITIONS "setup=cydtft_setup;loop=cydtft_loop")
