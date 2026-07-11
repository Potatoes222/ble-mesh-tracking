#pragma once

#include <stdint.h>

#include "esp_ble_mesh_defs.h"
#define BMT_CID_ESP 0x02E5
#define BMT_VND_MODEL_ID 0x0000

#define BMT_OP_VND_TAG_STATUS ESP_BLE_MESH_MODEL_OP_3(0x00, BMT_CID_ESP)
#define BMT_OP_VND_RESET_CMD ESP_BLE_MESH_MODEL_OP_3(0x05, BMT_CID_ESP)
#define BMT_OP_VND_OTA_TRIGGER ESP_BLE_MESH_MODEL_OP_3(0x06, BMT_CID_ESP)  /* lệnh OTA WiFi */
#define BMT_OP_VND_OTA_RESULT ESP_BLE_MESH_MODEL_OP_3(0x07, BMT_CID_ESP)   /* node báo cáo OTA thành công/thất bại */
#define BMT_OP_VND_OTA_KEY_PUSH ESP_BLE_MESH_MODEL_OP_3(0x08, BMT_CID_ESP) /* [SECURITY] Gateway push OTA-beacon HMAC key moi (16 byte) — da duoc mesh AppKey ma hoa san */

#define BMT_NODE_TYPE_SCANNER 0x01
#define BMT_NODE_TYPE_RELAY 0x02
#define BMT_NODE_TYPE_ALL 0xFF

#pragma pack(1)
typedef struct
{
	uint8_t scanner_id;
	uint8_t tag_type;
	uint16_t tag_id;
	int8_t rssi;
	int16_t distance_dm;
	uint8_t loss_pct;
} bmt_tag_report_t;

typedef struct
{
	uint8_t status; /* 0 = OTA thành công, khác 0 = thất bại (xem log lý do bên node) */
} bmt_ota_result_t;
#pragma pack()
