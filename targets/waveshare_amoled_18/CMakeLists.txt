# The Waveshare ESP32-S3-Touch-AMOLED-1.8 board (480x480 AMOLED, two side
# buttons + PWR). Only board variant of the agentgauge firmware today; reuses
# the shared agentgauge_fw library and only adds its own hardware profile in
# board.cpp.

if(NOT TARGET agentgauge_fw)
    message(WARNING "agentgauge_fw not built; skipping waveshare_amoled_18 target")
    return()
endif()

add_board_variant(waveshare_amoled_18 agentgauge_fw)
