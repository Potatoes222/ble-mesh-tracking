set(srcs
    "main.c"
    "bmt_mesh.c"
    "bmt_ota.c"
    "bmt_uart.c"
)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bt
        app_update
        esp_rom
        esp_system
        example_init
        esp_driver_uart
        esp_wifi
        esp_http_client
        esp_https_ota
        esp-tls
)