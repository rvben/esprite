# Waveshare AMOLED 2.16 C6 board (480x480). A board variant of the Clawdmeter
# firmware: it reuses the shared clawdmeter_fw library and only adds its own
# board profile in board.cpp.

if(NOT TARGET clawdmeter_fw)
    message(WARNING "clawdmeter_fw not built; skipping waveshare_amoled_216_c6")
    return()
endif()

add_board_variant(waveshare_amoled_216_c6 clawdmeter_fw)
