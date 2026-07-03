#pragma once

#include <stdint.h>
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"

static const uint8_t BMT_SYSTEM_UUID[16] = {
    0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#define BMT_TAG_MAJOR 0x0001
#define BMT_TAG_MINOR 0x0001

#define BMT_TAG_TX_POWER_REF (-53)
#define BMT_TAG_RADIO_PWR    ESP_PWR_LVL_N0

#define BMT_ADV_INTERVAL_MIN_MS 450
#define BMT_ADV_INTERVAL_MAX_MS 550
#define BMT_SEQ_TICK_MS         500
