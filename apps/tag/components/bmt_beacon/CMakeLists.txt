idf_component_register(
    SRCS "bmt_beacon.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmt_auth bmt_config
                  bt esp_timer
)
