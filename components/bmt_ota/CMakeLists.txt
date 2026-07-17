idf_component_register(
    SRCS "bmt_ota.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_mesh
                  app_update esp_http_client esp_https_ota bootloader_support
                  esp_wifi esp_event esp_netif nvs_flash esp-tls mbedtls bt
    EMBED_TXTFILES "ota_ca.pem"
)
