#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
esp_err_t bmt_ota_gateway_self_update(void);
esp_err_t bmt_ota_trigger_all_relays(void);
esp_err_t bmt_ota_trigger_all_scanners(void);

bool bmt_ota_is_running(void);

/* Gọi 1 lần lúc boot (main.c), trước khi UART/RPC có thể trigger OTA scanner */
void bmt_ota_beacon_key_init(void);
void bmt_ota_start_auto_check(void);
void bmt_ota_start_key_rotation(void);
void bmt_ota_push_beacon_key_to_node(uint16_t addr);
