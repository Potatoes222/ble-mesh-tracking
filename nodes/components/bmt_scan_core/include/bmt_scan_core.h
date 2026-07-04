#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"

typedef struct {
    uint8_t     scanner_id;
    const char *wifi_ssid;
    const char *wifi_pass;
    const char *ota_url;
} bmt_scan_core_cfg_t;

esp_err_t   bmt_scan_core_init(const bmt_scan_core_cfg_t *cfg);
uint8_t     bmt_scan_core_scanner_id(void);
const char *bmt_scan_core_wifi_ssid(void);
const char *bmt_scan_core_wifi_pass(void);
const char *bmt_scan_core_ota_url(void);
