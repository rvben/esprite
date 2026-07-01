# Waveshare AMOLED 1.8 board (368x448). A board variant of the Clawdmeter
# firmware: it reuses the shared clawdmeter_fw library and only adds its own
# board profile in board.cpp. board_caps() reports the 1.8 panel at runtime, so
# no firmware is recompiled.

if(NOT TARGET clawdmeter_fw)
    message(WARNING "clawdmeter_fw not built; skipping waveshare_amoled_18")
    return()
endif()

add_board_variant(waveshare_amoled_18 clawdmeter_fw)
