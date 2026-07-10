idf_component_register(
    SRCS "bmt_mesh.c"
    INCLUDE_DIRS "."
    REQUIRES bt
    PRIV_REQUIRES bmt_auth bmt_ota bmt_scan_core bmt_tag_table bmt_types
                  nvs_flash esp_system
)
