idf_component_register(
    SRCS
        "main.c"
        "bmt_beacon.c"
        "bmt_uart.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        nvs_flash
        bt
        esp_system
        esp_driver_uart
)
