idf_component_register(
    SRCS "bmt_mesh.c"
    INCLUDE_DIRS "."
    REQUIRES bt
    PRIV_REQUIRES bmt_config bmt_ota bmt_types
                  nvs_flash esp_system
)
