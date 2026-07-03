#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_crc.h"
#include "bmt_mesh.h"
#include "bmt_scan_core.h"
#include "bmt_tag_table.h"
#include "bmt_types.h"

#define BMT_GAP_SCAN_DURATION_MS     1200
#define BMT_MESH_PUBLISH_DURATION_MS 300
#define BMT_SCAN_INTERVAL_UNITS      0x0010
#define BMT_SCAN_WINDOW_UNITS        0x0010
#define BMT_CID_APPLE                0x004C
#define BMT_TAG_MAJOR_PERSON         0x0001
#define BMT_TAG_MAJOR_ASSET          0x0002
#define BMT_PHONE_TX_POWER_1M        (-59)

typedef enum {
    BMT_PHASE_GAP_SCAN = 0,
    BMT_PHASE_MESH_PUB = 1,
} bmt_radio_phase_t;

#pragma pack(1)
typedef struct {
    uint8_t  uuid[16];
    uint16_t major;
    uint16_t minor;
    int8_t   tx_power;
    uint8_t  sequence;
    uint16_t crc16;
} bmt_adv_esp_t;
#pragma pack()

typedef struct {
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   tx_power;
    uint8_t  sequence;
} bmt_parsed_t;

esp_err_t         bmt_scan_start(void);
bmt_radio_phase_t bmt_scan_get_phase(void);
