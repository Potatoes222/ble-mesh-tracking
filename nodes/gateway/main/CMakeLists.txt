idf_component_register(
    SRCS "bmt_zone.c" "bmt_scan_list.c" "bmt_node_table.c" "bmt_mac_cache.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bt
        esp_wifi
        esp_event
        esp_netif
        mqtt
        esp_driver_uart
        example_init
    EMBED_TXTFILES
        "ca.pem"
)