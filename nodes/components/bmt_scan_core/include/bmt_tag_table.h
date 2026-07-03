#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_types.h"

#define BMT_MAX_TAGS               20
#define BMT_TAG_TIMEOUT_MS         5000
#define BMT_PATH_LOSS_N            2.5f
#define BMT_LOG_RSSI_THRESHOLD_DBM 3
#define BMT_LOG_MIN_INTERVAL_MS    2000

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

typedef struct {
    float q, r, x, p, k;
} bmt_kalman_t;

typedef struct {
    bool     active;
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   tx_power;
    int8_t   rssi_raw;
    float    rssi_filtered;
    float    distance;
    uint8_t  last_sequence;
    uint32_t total_received;
    uint32_t total_missed;
    uint32_t last_seen_ms;
    uint8_t  mac[6];
    int8_t   last_logged_rssi;
    uint32_t last_log_ms;
} bmt_tag_entry_t;

void bmt_tag_table_reset(void);
void bmt_tag_table_update(const bmt_tag_hit_t *hit);
void bmt_tag_table_check_timeouts(void);
void bmt_tag_table_print(uint8_t scanner_id);

bool bmt_tag_table_has_new_data(void);
void bmt_tag_table_clear_new_data(void);

bool bmt_tag_table_get_snapshot(int index, bmt_tag_snapshot_t *out);
