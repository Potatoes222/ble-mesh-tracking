idf_component_register(
    SRCS
        "main.c"
        "bmt_mesh.c"
        "bmt_ota.c"
        "bmt_uart.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bt
        esp_driver_uart
        esp_wifi
        esp_event
        esp_netif
        esp_https_ota
        esp_http_client
        esp-tls
)
