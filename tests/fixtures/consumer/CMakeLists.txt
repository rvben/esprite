cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX C)

include(FetchContent)
# Consume the esprite tree under test (path injected by the driver script).
FetchContent_Declare(esprite SOURCE_DIR ${ESPRITE_SOURCE_DIR})
FetchContent_MakeAvailable(esprite)

esprite_add_sim_target(consumer
    ${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    TARGET_DIRECTORY target_consumer
    PROPERTIES COMPILE_DEFINITIONS "setup=consumer_setup;loop=consumer_loop")

esprite_add_runner(consumer-runner TARGET consumer)
