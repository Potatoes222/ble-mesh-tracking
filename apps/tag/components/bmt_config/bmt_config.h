#pragma once

#include <stdint.h>
#include "esp_bt.h"

/* ============================================================================
 * BMT_CONFIG — USER CONFIG cho Tag, ĐỔI CHO TỪNG THIẾT BỊ
 * ============================================================================ */

/* System UUID — GIỐNG NHAU trên tất cả tag (ESP32 + iPhone)
 * Scanner check 4 bytes đầu (AB 00 00 00) để nhận ra "tag của hệ thống" */
static const uint8_t BMT_SYSTEM_UUID[16] = {
    0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Major: loại tag
 * 0x0001 = PERSON (người đeo)
 * 0x0002 = ASSET  (tài sản gắn thiết bị) */
#define BMT_TAG_MAJOR 0x0001 /* ← đổi cho từng loại */

/* Minor: ID thiết bị trong hệ thống
 * Tag ESP32 #1 = 0x0001, #2 = 0x0002...
 * iPhone RNF: set cùng Minor tương ứng */
#define BMT_TAG_MINOR 0x0001 /* ← đổi cho từng thiết bị */

/* TX Power reference: RSSI đo được tại đúng 1m (giá trị calibrate)
 * KHÔNG phải radio TX power thực — đây là "Measured Power" như iBeacon
 * Mặc định -59 (iBeacon standard), cần đo thực tế để chỉnh lại */
#define BMT_TAG_TX_POWER (-53) /* ← calibrate: đo RSSI tại 1m rồi điền */

/* Radio TX power thực (ảnh hưởng range + pin)
 * ESP_PWR_LVL_N0 = 0 dBm → range ~10-15m indoor, phù hợp cho hệ thống này */
#define BMT_TAG_RADIO_PWR ESP_PWR_LVL_N0

/* ADV interval random trong range này để tránh collision nhiều tag */
#define BMT_ADV_INTERVAL_MIN_MS 450
#define BMT_ADV_INTERVAL_MAX_MS 550
