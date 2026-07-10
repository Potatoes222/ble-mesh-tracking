#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* ============================================================================
 * PAYLOAD STRUCT — so sánh với iBeacon:
 *
 *   iBeacon (Apple CID 0x004C):
 *     CID(2) + 0215(2) + UUID(16) + Major(2) + Minor(2) + TXPwr(1) = 25B
 *
 *   Custom (Espressif CID 0x02E5):
 *     CID(2) + UUID(16) + Major(2) + Minor(2) + TXPwr(1) + Seq(1) + MAC(2) = 26B
 *
 *   Bỏ "02 15" marker (chỉ cần cho iOS CLBeaconRegion, không cần với scanner)
 *   → tiết kiệm 2B → có chỗ cho Seq + MAC, tổng ADV = 31B (vừa khít)
 *
 * iPhone dùng RNF Beacon Toolkit: Set UUID = AB000000-..., Major = 1 (PERSON),
 * Minor = tag_id. Scanner detect qua CID 0x004C → không có Seq/MAC → loss = 0%
 * ============================================================================ */
#pragma pack(1)
typedef struct {
    uint8_t  uuid[16];   /* 16B: system UUID (AB000000-...) */
    uint16_t major;      /*  2B: PERSON=0x0001, ASSET=0x0002 */
    uint16_t minor;      /*  2B: tag ID trong hệ thống */
    int8_t   tx_power;   /*  1B: measured power tại 1m (calibrate) */
    uint8_t  sequence;   /*  1B: 0–255, wraps → Scanner tính loss rate */
    uint16_t mac16;      /*  2B: HMAC-SHA256 rút gọn (thay CRC16) */
} bmt_tag_adv_payload_t;  /* = 24 bytes */
#pragma pack()

/* Khởi tạo Bluetooth, đăng ký GAP callback, tạo timer sequence 500ms,
 * bắt đầu advertise. PHẢI gọi sau bmt_auth_init() */
esp_err_t bmt_beacon_start(void);

/* Getter cho UART status */
uint8_t  bmt_beacon_sequence(void);
uint16_t bmt_beacon_last_mac16(void);
bool     bmt_beacon_is_active(void);
