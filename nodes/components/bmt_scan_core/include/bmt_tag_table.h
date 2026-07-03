#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BMT_MAX_TAGS       20
#define BMT_TAG_TIMEOUT_MS 5000

typedef struct {
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   tx_power;
    int8_t   rssi;
    uint8_t  sequence;
    uint8_t  mac[6];
} bmt_tag_hit_t;

typedef struct {
    bool     active;
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   rssi_int;
    float    rssi_filtered;
    float    distance;
    uint32_t total_received;
    uint32_t total_missed;
} bmt_tag_snapshot_t;

void bmt_tag_table_reset(void);
void bmt_tag_table_update(const bmt_tag_hit_t *hit);
void bmt_tag_table_check_timeouts(void);
void bmt_tag_table_print(uint8_t scanner_id);

bool bmt_tag_table_has_new_data(void);
void bmt_tag_table_clear_new_data(void);

bool bmt_tag_table_get_snapshot(int index, bmt_tag_snapshot_t *out);
