idf_component_register(
    SRCS "bmt_wifi.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_config esp_wifi esp_event esp_netif nvs_flash
)
