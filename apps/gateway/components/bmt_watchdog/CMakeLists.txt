idf_component_register(
    SRCS "bmt_watchdog.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_mesh bmt_node_table bmt_ota bmt_types bmt_zone
                  esp_timer esp_system
)
