set(srcs 
    "main.c"
   
)

idf_component_register(
    SRCS "bmt_uart.c" "bmt_beacon.c" "bmt_auth.c" "${srcs}"
    INCLUDE_DIRS "."
    REQUIRES 
        driver 
        nvs_flash
        bt
        esp_system
        esp_driver_uart
        mbedtls
)