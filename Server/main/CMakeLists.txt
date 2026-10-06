idf_component_register(
    SRCS
        "main.c"
        "display/lcd_display.c"
        "parking/parking_state.c"
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
        esp_lcd
        esp_driver_spi
        esp_driver_gpio
        lwip
        nvs_flash
)
