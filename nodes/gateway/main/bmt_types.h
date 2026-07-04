#pragma once

#include <stdint.h>

#include "esp_ble_mesh_defs.h"

#define BMT_CID_ESP            0x02E5
#define BMT_VND_MODEL_ID       0x0000
#define BMT_OP_VND_TAG_STATUS  ESP_BLE_MESH_MODEL_OP_3(0x00, BMT_CID_ESP)
#define BMT_OP_VND_NODE_HEALTH ESP_BLE_MESH_MODEL_OP_3(0x03, BMT_CID_ESP)
#define BMT_OP_VND_RESET_CMD   ESP_BLE_MESH_MODEL_OP_3(0x05, BMT_CID_ESP)
#define BMT_OP_VND_OTA_TRIGGER ESP_BLE_MESH_MODEL_OP_3(0x06, BMT_CID_ESP)

#define BMT_NODE_TYPE_SCANNER 0x01
#define BMT_NODE_TYPE_RELAY   0x02
#define BMT_NODE_TYPE_ALL     0xFF

typedef struct __attribute__((packed)) {
    uint8_t node_type;
} bmt_ota_trigger_t;

typedef struct __attribute__((packed)) {
    uint8_t  scanner_id;
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   rssi;
    int16_t  distance_dm;
    uint8_t  loss_pct;
} bmt_tag_report_t;

typedef struct __attribute__((packed)) {
    uint8_t  scanner_id;
    int8_t   chip_temp_c;
    uint16_t vdd_mv;
    uint16_t free_heap_kb;
    uint16_t uptime_min;
} bmt_node_health_t;
