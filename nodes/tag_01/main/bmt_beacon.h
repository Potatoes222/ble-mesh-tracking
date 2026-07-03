#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t bmt_beacon_init(void);

uint8_t  bmt_beacon_get_sequence(void);
uint16_t bmt_beacon_get_crc16(void);
bool     bmt_beacon_is_active(void);
