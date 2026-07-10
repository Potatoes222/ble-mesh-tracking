idf_component_register(
    SRCS "bmt_uart.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_beacon bmt_config
                  esp_driver_uart esp_system
)
