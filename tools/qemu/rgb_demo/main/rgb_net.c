// Fixture networking: the OpenCores ethernet the QEMU machine emulates
// (esprite forwards a host port to guest port 80 via user-net hostfwd), DHCP
// from QEMU's built-in user network, and a tiny HTTP server whose POST
// /snapshot recolors the fixture's marker square - proving esprite's
// snapshot data path end to end: host TCP -> NAT -> emulated NIC -> lwIP ->
// this handler -> panel.
#include <stdio.h>
#include <string.h>
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"

// Written by the HTTP task, polled by the render loop: the same
// event-counter pattern the input agent uses, so a POST is never missed
// while the renderer is blocked in a frame draw.
volatile int g_marker_color = 0;
volatile int g_marker_events = 0;

static esp_err_t snapshot_post(httpd_req_t* req) {
    char body[256];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';
    cJSON* doc = cJSON_Parse(body);
    const cJSON* color = doc ? cJSON_GetObjectItem(doc, "color") : NULL;
    if (!cJSON_IsNumber(color) || color->valueint < 0 || color->valueint > 0xFFFF) {
        cJSON_Delete(doc);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need {\"color\": 0..65535}");
        return ESP_FAIL;
    }
    g_marker_color = color->valueint;
    g_marker_events++;
    cJSON_Delete(doc);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static void on_got_ip(void* arg, esp_event_base_t base, int32_t id, void* data) {
    (void)arg; (void)base; (void)id; (void)data;
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    if (httpd_start(&server, &cfg) == ESP_OK) {
        static const httpd_uri_t uri = {
            .uri = "/snapshot", .method = HTTP_POST, .handler = snapshot_post,
        };
        httpd_register_uri_handler(server, &uri);
        printf("rgb_demo http ready\n");
    } else {
        printf("rgb_demo http start failed\n");
    }
}

void rgb_net_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t* eth_netif = esp_netif_new(&netif_cfg);

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    // The open_eth QEMU model answers as a DP83848 at PHY address 1 with no
    // real autonegotiation; a short timeout keeps bring-up fast.
    phy_config.phy_addr = 1;
    phy_config.autonego_timeout_ms = 100;
    phy_config.reset_gpio_num = -1;
    esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
}
