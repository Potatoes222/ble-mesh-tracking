#pragma once

#include "esp_err.h"

typedef enum {
    BMT_PHASE_GAP_SCAN = 0,
    BMT_PHASE_MESH_PUB = 1,
} bmt_radio_phase_t;

esp_err_t         bmt_scan_start(void);
bmt_radio_phase_t bmt_scan_get_phase(void);
