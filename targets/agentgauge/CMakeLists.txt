# The agentgauge board (480x480 AMOLED, two side buttons + PWR). Only board
# variant of the agentgauge firmware today; reuses the shared agentgauge_fw
# library and only adds its own hardware profile in board.cpp.

if(NOT TARGET agentgauge_fw)
    message(WARNING "agentgauge_fw not built; skipping agentgauge target")
    return()
endif()

add_board_variant(agentgauge agentgauge_fw)
