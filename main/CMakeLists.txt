idf_component_register(
    SRCS
        "main.c"
        "server/server.c"

    EMBED_TXTFILES
        "server/index.html"
        "server/style.css"

    INCLUDE_DIRS
        "."
        "server"

    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        nvs_flash
)
