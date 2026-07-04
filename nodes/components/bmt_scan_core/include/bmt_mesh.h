#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "bmt_ota.h"
#include "bmt_scan_core.h"
#include "bmt_types.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define BMT_GW_UNICAST_ADDR 0x0001

esp_err_t bmt_mesh_init(void);

bool     bmt_mesh_is_provisioned(void);
bool     bmt_mesh_is_ready(void);
uint16_t bmt_mesh_node_addr(void);
uint16_t bmt_mesh_app_idx(void);
uint16_t bmt_mesh_net_idx(void);

esp_err_t bmt_mesh_publish_tag_report(const bmt_tag_report_t *r);
esp_err_t bmt_mesh_publish_node_health(const bmt_node_health_t *h);

void bmt_mesh_reset(void);
