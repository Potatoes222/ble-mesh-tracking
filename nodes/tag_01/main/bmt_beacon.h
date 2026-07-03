#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "bmt_config.h"

#define BMT_ADV_RAW_LEN     31
#define BMT_ADV_PAYLOAD_OFF 7

#pragma pack(1)
typedef struct {
    uint8_t  uuid[16];
    uint16_t major;
    uint16_t minor;
    int8_t   tx_power;
    uint8_t  sequence;
    uint16_t crc16;
} bmt_adv_payload_t;
#pragma pack()

esp_err_t bmt_beacon_init(void);

uint8_t  bmt_beacon_get_sequence(void);
uint16_t bmt_beacon_get_crc16(void);
bool     bmt_beacon_is_active(void);
