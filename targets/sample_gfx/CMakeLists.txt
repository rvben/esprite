# sample_gfx target: a minimal Arduino_GFX sketch with no custom HAL. Proves the
# framework runs standard-library apps with zero app-specific sim code.

add_sim_target(sample_gfx
    ${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp
)

# Rename the Arduino entry points so they do not clash with other targets.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    TARGET_DIRECTORY target_sample_gfx
    PROPERTIES COMPILE_DEFINITIONS "setup=samplegfx_setup;loop=samplegfx_loop")
