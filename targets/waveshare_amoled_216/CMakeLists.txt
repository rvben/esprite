# The S3 AMOLED 2.16 board variant: same shared Clawdmeter firmware, its own
# board profile (rotation-capable, FWD secondary button).
if(TARGET clawdmeter_fw)
    add_board_variant(waveshare_amoled_216 clawdmeter_fw)
else()
    message(WARNING "clawdmeter_fw not built; skipping waveshare_amoled_216")
endif()
