# The BLE Hardware Buddy flavor: the same firmware with app_wifi swapped for
# app_buddy + protocol, mirroring upstream's build_src_filter envs. main.cpp
# is compiled a second time so both flavors coexist in one binary; the renames
# cover its entry points and the three transport hooks (no other buddy source
# defines those names, so target-wide definitions are safe). Everything else
# resolves against clawdmeter_fw. The radio layer is the sim's virtual BLE
# link (board_sim/ble.cpp).
if(NOT EXISTS ${CM}/app_buddy.cpp OR NOT EXISTS ${CM}/protocol.cpp)
    message(STATUS "Clawdmeter at ${CM} predates the BLE buddy transport; skipping clawdmeter_buddy")
    return()
endif()

add_library(clawdmeter_buddy STATIC
    ${CM}/main.cpp
    ${CM}/app_buddy.cpp
    ${CM}/protocol.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../board_sim/ble.cpp
)
target_compile_definitions(clawdmeter_buddy PRIVATE
    setup=clawdmeter_buddy_setup
    loop=clawdmeter_buddy_loop
    app_init=buddy_app_init
    app_poll=buddy_app_poll
    app_buttons=buddy_app_buttons)
target_link_libraries(clawdmeter_buddy PUBLIC clawdmeter_fw)
