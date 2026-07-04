#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_ota.h"
#include "bmt_types.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define BMT_PROV_COMPLETE_BIT BIT0

esp_err_t bmt_mesh_init(void);

void bmt_mesh_reset(void);
void bmt_mesh_print_status(const char *title);
void bmt_mesh_start_monitor(void);

uint16_t bmt_mesh_get_node_addr(void);
