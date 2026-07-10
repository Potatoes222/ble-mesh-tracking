idf_component_register(
    SRCS "bmt_thingsboard.c"
    INCLUDE_DIRS "."
    REQUIRES bmt_types
    PRIV_REQUIRES bmt_config bmt_mqtt bmt_node_table bmt_zone json
)
