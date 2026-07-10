idf_component_register(
    SRCS "bmt_ota.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_config bmt_mesh bmt_node_table bmt_types
                  app_update esp_http_client esp_https_ota bootloader_support
                  esp_wifi nvs_flash mbedtls bt
)
