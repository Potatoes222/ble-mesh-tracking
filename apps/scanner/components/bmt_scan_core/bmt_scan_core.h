#pragma once

#include <stdint.h>

#include "esp_err.h"
#define BMT_SCAN_NVS_NAMESPACE "bmt_scan"

/* Call once at the top of app_main(). Orchestrates the full init in
 * the right order: NVS scanner_id -> auth (HMAC key) -> bluetooth
 * -> mesh -> scan (GAP) -> uart. */
esp_err_t bmt_scan_core_init(void);

uint8_t bmt_scan_core_scanner_id(void);
void bmt_scan_core_set_scanner_id(uint8_t id);
