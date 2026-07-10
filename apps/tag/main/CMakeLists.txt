idf_component_register(
    SRCS "main.c"
    REQUIRES bmt_auth bmt_beacon bmt_config bmt_uart
    PRIV_REQUIRES nvs_flash
)
