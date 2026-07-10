idf_component_register(
    SRCS "bmt_tag_table.c"
    INCLUDE_DIRS "."
    REQUIRES bmt_types
    PRIV_REQUIRES esp_timer
)
