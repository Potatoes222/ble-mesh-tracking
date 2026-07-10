#pragma once

#include <stdint.h>

#include "esp_ble_mesh_defs.h"

#define BMT_CID_ESP                     0x02E5
#define BMT_VND_MODEL_ID                0x0000

/* Opcode phải trùng với Gateway/Scanner */
#define BMT_OP_VND_RESET_CMD            ESP_BLE_MESH_MODEL_OP_3(0x05, BMT_CID_ESP)
#define BMT_OP_VND_OTA_TRIGGER          ESP_BLE_MESH_MODEL_OP_3(0x06, BMT_CID_ESP) /* WiFi OTA */
#define BMT_OP_VND_OTA_RESULT           ESP_BLE_MESH_MODEL_OP_3(0x07, BMT_CID_ESP) /* tự báo cáo OTA thành công/thất bại về Gateway */

#pragma pack(1)
typedef struct {
    uint8_t status;   /* 0 = OTA thành công, khác 0 = thất bại */
} bmt_ota_result_t;
#pragma pack()
