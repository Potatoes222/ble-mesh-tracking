#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BMT_PROV_COMPLETE_BIT BIT0

/* Initialise the mesh: register callbacks, build UUID from MAC and
 * scanner_id, init composition, either restore provisioning from NVS
 * or enable fresh provisioning. */
esp_err_t bmt_mesh_init(void);

/* Event group used to wait for / test BMT_PROV_COMPLETE_BIT (e.g. by
 * bmt_scan.c before it starts processing tag ADVs). */
EventGroupHandle_t bmt_mesh_evgrp(void);

uint16_t bmt_mesh_node_addr(void);
uint16_t bmt_mesh_app_idx(void);

/* Pointer to the current 16-byte UUID (SCAN + MAC + ... + scanner_id).
 * Read-only: use for log/status; do not modify directly. */
const uint8_t* bmt_mesh_uuid(void);

/* Walk every active tag in bmt_tag_table and publish TAG_STATUS to mesh. */
void bmt_mesh_publish_tags(void);

/* Report an OTA result back to the Gateway over mesh
 * (0 = success, non-zero = failure). */
esp_err_t bmt_mesh_report_ota_result(uint8_t status);

/* Reset mesh provisioning (used by UART 'r' and by RESET_CMD from
 * the Gateway). */
void bmt_mesh_local_reset(void);
