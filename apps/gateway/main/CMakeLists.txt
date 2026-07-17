idf_component_register(
    SRCS "main.c"
    REQUIRES
        bmt_config bmt_factory_reset bmt_mesh bmt_mqtt bmt_node_table bmt_ota
        bmt_thingsboard bmt_uart bmt_watchdog bmt_wifi bmt_zone
        nvs_flash bt esp_system example_init
)
