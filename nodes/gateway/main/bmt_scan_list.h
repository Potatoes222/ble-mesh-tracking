#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_node_table.h"

typedef enum {
    BMT_PROV_MODE_AUTO   = 0,
    BMT_PROV_MODE_MANUAL = 1,
} bmt_prov_mode_t;

typedef struct {
    bool     used;
    uint8_t  uuid[16];
    uint8_t  addr[6];
    uint8_t  addr_type;
    uint16_t oob_info;
} bmt_scan_entry_t;

void bmt_scan_list_reset(void);
bool bmt_scan_list_add(const uint8_t *uuid, const uint8_t *mac, uint8_t addr_type,
                       uint16_t oob_info);
void bmt_scan_list_print(void);
void bmt_scan_list_do_scan(void);
void bmt_scan_list_provision(void);

bmt_prov_mode_t bmt_scan_list_get_mode(void);
void            bmt_scan_list_set_mode(bmt_prov_mode_t mode);
bool            bmt_scan_list_is_scanning(void);
