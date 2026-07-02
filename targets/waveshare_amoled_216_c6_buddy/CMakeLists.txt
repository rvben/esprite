# The C6 AMOLED board running the Clawdmeter BLE Hardware Buddy flavor. Only
# registered when the buddy firmware library exists (firmware new enough to
# carry app_buddy + protocol).
if(TARGET clawdmeter_buddy)
    add_board_variant(waveshare_amoled_216_c6_buddy clawdmeter_buddy)
else()
    message(WARNING "clawdmeter_buddy not built; skipping waveshare_amoled_216_c6_buddy")
endif()
