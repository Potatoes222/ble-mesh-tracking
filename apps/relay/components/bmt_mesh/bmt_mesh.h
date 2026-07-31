#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BMT_PROV_COMPLETE_BIT BIT0
esp_err_t bmt_mesh_init(void);

EventGroupHandle_t bmt_mesh_evgrp(void);

uint16_t bmt_mesh_node_addr(void);
uint16_t bmt_mesh_net_idx(void);
uint16_t bmt_mesh_app_idx(void);
bool bmt_mesh_relay_enabled(void);

/* Report an OTA result back to the Gateway over mesh
 * (0 = success, non-zero = failure). */
esp_err_t bmt_mesh_report_ota_result(uint8_t status);

/* Reset mesh provisioning (used by UART 'r' and by RESET_CMD from
 * the Gateway). */
void bmt_mesh_local_reset(void);
