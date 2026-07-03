#pragma once

#include <stdbool.h>
#include <stdint.h>

const char *bmt_zone_name(uint8_t scanner_id);

void bmt_zone_ingest_report(uint8_t scanner_id, uint16_t tag_id, uint8_t tag_type, int8_t rssi);
void bmt_zone_print_all(void);
void bmt_zone_start_timeout_task(void);
