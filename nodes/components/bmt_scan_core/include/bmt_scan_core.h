#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"

esp_err_t bmt_scan_core_init(uint8_t scanner_id);
uint8_t   bmt_scan_core_scanner_id(void);
