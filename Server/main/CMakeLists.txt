idf_component_register(
    SRCS
        "main.c"
        "network/network_service.c"
        "network/wifi_ap.c"
        "network/http/http_server.c"
        "network/udp/udp_server.c"

    EMBED_TXTFILES
        "network/http/index.html"
        "network/http/style.css"

    INCLUDE_DIRS
        "."
        "network"

    REQUIRES
        freertos
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        lwip
        nvs_flash
)
