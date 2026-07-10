idf_component_register(
    SRCS "bmt_uart.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_mesh bmt_ota bmt_scan_core bmt_types
                  esp_driver_uart esp_system
)
