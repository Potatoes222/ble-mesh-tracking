idf_component_register(
    SRCS "main.c"
    REQUIRES bmt_scan_core bmt_factory_reset
    PRIV_REQUIRES nvs_flash
)
