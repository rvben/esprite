idf_component_register(SRCS "rgb_demo.c" "rgb_net.c" INCLUDE_DIRS ""
                       PRIV_REQUIRES esp_eth esp_netif esp_event esp_http_server json
                                     esprite_qemu_agent)
