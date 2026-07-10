set(srcs
    "main.c"
    "bmt_scan_core.c"
    "bmt_auth.c"
    "bmt_tag_table.c"
    "bmt_mesh.c"
   
    "bmt_scan.c"
    "bmt_uart.c"
)

idf_component_register(
    SRCS "bmt_ota.c" "${srcs}"
    INCLUDE_DIRS "."
    REQUIRES
    esp_timer
    esp_wifi
     esp_http_client
      esp_https_ota
       esp-tls
        driver 
        esp_system
        nvs_flash
        bt
        app_update
        esp_driver_uart
        mbedtls
)
