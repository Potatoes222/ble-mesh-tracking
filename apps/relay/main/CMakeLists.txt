idf_component_register(
    SRCS "main.c"
    REQUIRES
        bmt_config bmt_factory_reset bmt_mesh bmt_ota bmt_uart
        nvs_flash bt esp_system example_init
)
