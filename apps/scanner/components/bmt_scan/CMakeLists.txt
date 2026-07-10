idf_component_register(
    SRCS "bmt_scan.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_auth bmt_mesh bmt_ota bmt_scan_core bmt_tag_table bmt_types
                  bt esp_timer
)
