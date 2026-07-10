idf_component_register(
    SRCS "main.c"
    REQUIRES bmt_scan_core
    PRIV_REQUIRES nvs_flash
)
