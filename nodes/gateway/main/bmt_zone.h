#pragma once

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_config.h"

typedef struct {
    bool     active;
    uint16_t tag_id;
    uint8_t  tag_type;
    int8_t   rssi_by_scanner[BMT_MAX_SCANNERS];
    uint32_t ts_by_scanner[BMT_MAX_SCANNERS];
    bool     valid_by_scanner[BMT_MAX_SCANNERS];
    uint8_t  current_zone_id;
    uint32_t last_zone_change_ms;
    uint32_t last_any_report_ms;
} bmt_zone_track_t;

const char *bmt_zone_name(uint8_t scanner_id);

void bmt_zone_ingest_report(uint8_t scanner_id, uint16_t tag_id, uint8_t tag_type, int8_t rssi);
void bmt_zone_print_all(void);
void bmt_zone_start_timeout_task(void);
