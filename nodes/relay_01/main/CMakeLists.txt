idf_component_register(
    SRCS
        "main.c"
        "bmt_mesh.c"
        "bmt_uart.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bt
        esp_driver_uart
)
