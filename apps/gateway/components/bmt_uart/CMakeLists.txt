idf_component_register(
    SRCS "bmt_uart.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_config bmt_mesh bmt_mqtt bmt_node_table bmt_ota
                  bmt_scan_list bmt_thingsboard bmt_types bmt_zone
                  esp_driver_uart
)
