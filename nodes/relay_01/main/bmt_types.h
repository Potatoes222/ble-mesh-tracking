#pragma once

#include <stdint.h>

#include "esp_ble_mesh_defs.h"

#define BMT_VND_MODEL_ID       0x0000
#define BMT_OP_VND_RESET_CMD   ESP_BLE_MESH_MODEL_OP_3(0x05, BMT_CID_ESP)
#define BMT_OP_VND_OTA_TRIGGER ESP_BLE_MESH_MODEL_OP_3(0x06, BMT_CID_ESP)

#define BMT_NODE_TYPE_SCANNER 0x01
#define BMT_NODE_TYPE_RELAY   0x02
#define BMT_NODE_TYPE_ALL     0xFF

typedef struct __attribute__((packed)) {
    uint8_t node_type;
} bmt_ota_trigger_t;
