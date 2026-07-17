idf_component_register(
    SRCS "bmt_uart.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_config bmt_mesh
                  esp_driver_uart esp_system app_update
)
